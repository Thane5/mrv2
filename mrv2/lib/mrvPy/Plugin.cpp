// SPDX-License-Identifier: BSD-3-Clause
// mrv2
// Copyright Contributors to the mrv2 Project. All rights reserved.

#include <iostream>
#include <filesystem>
namespace fs = std::filesystem;

#include <pybind11/pybind11.h>
namespace py = pybind11;

#include <FL/Fl_Menu.H>

#include <tlCore/StringFormat.h>

#include "mrvCore/mrvHome.h"

#include "mrvFl/mrvIO.h"
#include "mrvFl/mrvMenus.h"

namespace
{
    const char* kModule = "python";
    const std::string kPattern = ".py";
} // namespace

namespace mrv
{
    using namespace tl;

    void process_python_plugin(const std::string& file)
    {
        try
        {
            std::string moduleName = file.substr(0, file.size() - 3);

            py::module module = py::module::import(moduleName.c_str());

            py::object pluginObj = module.attr("Plugin")();

            py::dict menuDict = pluginObj.attr("get_menu_entries")();

            for (const auto& item : menuDict)
            {
                std::string menu = py::cast<std::string>(item.first);
                py::handle method = item.second;
                method.inc_ref();
                pythonMenus[menu] = method;
            }
        }
        catch (const std::exception& e)
        {
            LOG_ERROR(e.what());
        }
    }

    void discover_python_plugins()
    {
        std::unordered_map<std::string, std::string> plugins;
        const std::vector<std::string>& paths = python_plugin_paths();
        for (const auto& path : paths)
        {
            for (const auto& entry : fs::directory_iterator(path))
            {
                if (entry.is_regular_file() &&
                    fs::path(entry).extension() == kPattern)
                {
                    const std::string& file =
                        entry.path().filename().generic_string();
                    if (plugins.find(file) != plugins.end())
                    {
                        std::string err =
                            string::Format(_("Duplicated Python plugin {0} in "
                                             "{1} and {2}."))
                                .arg(file)
                                .arg(path)
                                .arg(plugins[file]);
                        LOG_ERROR(err);
                        continue;
                    }
                    plugins[file] = path;
                }
            }
        }

        // Import the sys module
        py::module sys = py::module::import("sys");

        // Access the sys.path list
        py::list sysPath = sys.attr("path");

        for (const auto& path : paths)
        {
            // Add the additional directory to the sys.path list
            sysPath.attr("append")(path);
        }

        for (const auto& plugin : plugins)
        {
            process_python_plugin(plugin.first);
        }
    }

    void run_python_method_cb(Fl_Menu_* m, void* d)
    {
        py::handle func = *(static_cast<py::handle*>(d));
        try
        {
            func();
        }
        catch (const std::exception& e)
        {
            LOG_ERROR(e.what());
        }
    }

} // namespace mrv

void mrv2_python_plugins(pybind11::module& m)
{
    using namespace mrv;

    discover_python_plugins();
}
