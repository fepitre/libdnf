/*
Copyright (C) 2020 Red Hat, Inc.

This file is part of dnfdaemon-server: https://github.com/rpm-software-management/libdnf/

Dnfdaemon-server is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

Dnfdaemon-server is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with dnfdaemon-server.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "session.hpp"

#include "dbus.hpp"
#include "services/base/base.hpp"
#include "services/repo/repo.hpp"
#include "services/repoconf/repo_conf.hpp"
#include "services/rpm/rpm.hpp"
#include "utils.hpp"

#include <libdnf/logger/logger.hpp>
#include <sdbus-c++/sdbus-c++.h>

#include <chrono>
#include <iostream>
#include <string>

class DbusRepoCB : public libdnf::rpm::RepoCB {
public:
    DbusRepoCB(sdbus::IObject * dbus_object) : dbus_object(dbus_object){};
    void start(const char * what) override {
        auto signal = dbus_object->createSignal(dnfdaemon::INTERFACE_BASE, dnfdaemon::SIGNAL_REPO_LOAD_START);
        signal << what;
        dbus_object->emitSignal(signal);
    }

    void end() override {
        auto signal = dbus_object->createSignal(dnfdaemon::INTERFACE_BASE, dnfdaemon::SIGNAL_REPO_LOAD_END);
        signal << "";
        dbus_object->emitSignal(signal);
    }

    int progress([[maybe_unused]] double total_to_download, [[maybe_unused]] double downloaded) override { return 0; }

    // TODO(mblaha): how to ask the user for confirmation?
    bool repokey_import(
        [[maybe_unused]] const std::string & id,
        [[maybe_unused]] const std::string & user_id,
        [[maybe_unused]] const std::string & fingerprint,
        [[maybe_unused]] const std::string & url,
        [[maybe_unused]] long int timestamp) override {
        return false;
    }

private:
    sdbus::IObject * dbus_object;
};


class StderrLogger : public libdnf::Logger {
public:
    explicit StderrLogger() {}
    void write(time_t, pid_t, Level, const std::string & message) noexcept override;
};

void StderrLogger::write(time_t, pid_t, Level, const std::string & message) noexcept {
    try {
        std::cerr << message << std::endl;
    } catch (...) {
    }
}

template <typename ItemType>
ItemType Session::session_configuration_value(const std::string & key, const ItemType & default_value) {
    return key_value_map_get(session_configuration, key, default_value);
}


Session::Session(sdbus::IConnection & connection, dnfdaemon::KeyValueMap session_configuration, std::string object_path)
    : connection(connection)
    , base(std::make_unique<libdnf::Base>())
    , session_configuration(session_configuration)
    , object_path(object_path) {

    // set-up log router for base
    auto & log_router = base->get_logger();
    log_router.add_logger(std::make_unique<StderrLogger>());

    // load configuration
    base->load_config_from_file();

    // set cachedir
    auto system_cache_dir = base->get_config().system_cachedir().get_value();
    base->get_config().cachedir().set(libdnf::Option::Priority::RUNTIME, system_cache_dir);
    // set variables
    auto & variables = base->get_variables();
    variables["arch"] = "x86_64";
    variables["basearch"] = "x86_64";
    variables["releasever"] = "32";

    // load repo configuration
    auto & rpm_repo_sack = base->get_rpm_repo_sack();
    rpm_repo_sack.new_repos_from_file();
    rpm_repo_sack.new_repos_from_dirs();

    // instantiate all services provided by the daemon
    services.emplace_back(std::make_unique<Base>(*this));
    services.emplace_back(std::make_unique<RepoConf>(*this));
    services.emplace_back(std::make_unique<Repo>(*this));
    services.emplace_back(std::make_unique<Rpm>(*this));

    // Register all provided services on d-bus
    for (auto & s : services) {
        s->dbus_register();
    }
}

Session::~Session() {
    // deregister dbus services
    for (auto & s : services) {
        s->dbus_deregister();
    }
    threads_manager.finish();
}

bool Session::read_all_repos(std::unique_ptr<sdbus::IObject> & dbus_object) {
    // TODO(mblaha): get flags from session configuration
    using LoadFlags = libdnf::rpm::SolvSack::LoadRepoFlags;
    auto flags =
        LoadFlags::USE_FILELISTS | LoadFlags::USE_PRESTO | LoadFlags::USE_UPDATEINFO | LoadFlags::USE_OTHER;
    //auto & logger = base->get_logger();
    auto & rpm_repo_sack = base->get_rpm_repo_sack();
    auto enabled_repos = rpm_repo_sack.new_query().ifilter_enabled(true);
    auto & solv_sack = base->get_rpm_solv_sack();
    bool retval = true;
    for (auto & repo : enabled_repos.get_data()) {
        repo->set_callbacks(std::make_unique<DbusRepoCB>(dbus_object.get()));
        try {
            repo->load();
            solv_sack.load_repo(*repo.get(), flags);
        } catch (const std::runtime_error & ex) {
            if (!repo->get_config()->skip_if_unavailable().get_value()) {
                retval = false;
                break;
            }
        }
    }
    return retval;
}

// explicit instantiation of session_configuration_value template
template std::string Session::session_configuration_value<std::string>(const std::string &, const std::string &);
template int Session::session_configuration_value<int>(const std::string &, const int &);
