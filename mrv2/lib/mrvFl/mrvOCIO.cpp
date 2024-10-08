// SPDX-License-Identifier: BSD-3-Clause
// mrv2
// Copyright Contributors to the mrv2 Project. All rights reserved.

#include <tlCore/StringFormat.h>

#include <tlTimeline/OCIOOptions.h>

#include "mrvCore/mrvI8N.h"
#include "mrvCore/mrvFile.h"

#include "mrvFl/mrvIO.h"

#include "mrvNetwork/mrvLUTOptions.h"

#include "mrViewer.h"

namespace
{
    const char* kModule = "ocio";
}

namespace mrv
{
    namespace ocio
    {
        std::string ocioDefault = "ocio://default";

        std::string ocioConfig()
        {
            ViewerUI* ui = App::ui;
            PreferencesUI* uiPrefs = ui->uiPrefs;
            const char* out = uiPrefs->uiPrefsOCIOConfig->value();
            if (!out)
                return "";
            return out;
        }

        void setOcioConfig(const std::string config)
        {
            ViewerUI* ui = App::ui;
            PreferencesUI* uiPrefs = ui->uiPrefs;
            if (config.empty())
            {
                throw std::runtime_error(
                    _("OCIO config file cannot be empty."));
            }

            if (config.substr(0, 7) != "ocio://")
            {
                if (!file::isReadable(config))
                {
                    std::string err =
                        string::Format(_("OCIO config '{0}' does not "
                                         "exist or is not readable."))
                            .arg(config);
                    throw std::runtime_error(err);
                }
            }

            const char* oldconfig = uiPrefs->uiPrefsOCIOConfig->value();
            if (oldconfig && strlen(oldconfig) > 0)
            {
                // Same config file.  Nothing to do.
                if (config == oldconfig)
                    return;
            }

            uiPrefs->uiPrefsOCIOConfig->value(config.c_str());
            Preferences::OCIO(ui);
        }

        std::string ocioIcs()
        {
            auto uiICS = App::ui->uiICS;
            int idx = uiICS->value();
            if (idx < 0 || idx >= uiICS->children())
                return "";

            const Fl_Menu_Item* item = uiICS->child(idx);
            if (!item || !item->label() || item->flags & FL_SUBMENU)
                return "";

            char pathname[1024];
            int ret = uiICS->item_pathname(pathname, 1024, item);
            if (ret != 0)
                return "";

            std::string ics = pathname;
            if (ics[0] == '/')
                ics = ics.substr(1, ics.size());

            return ics;
        }

        void setOcioIcs(const std::string& name)
        {
            auto uiICS = App::ui->uiICS;

            int value = -1;
            if (name.empty())
                value = 0;

            for (int i = 0; i < uiICS->children(); ++i)
            {
                const Fl_Menu_Item* item = uiICS->child(i);
                if (!item || !item->label() || item->flags & FL_SUBMENU)
                    continue;

                char pathname[1024];
                int ret = uiICS->item_pathname(pathname, 1024, item);
                if (ret != 0)
                    continue;

                if (name == pathname || name == item->label())
                {
                    value = i;
                    break;
                }
            }
            if (value == -1)
            {
                std::string err =
                    string::Format(_("Invalid OCIO Ics '{0}'.")).arg(name);
                throw std::runtime_error(err);
                return;
            }
            uiICS->value(value);
            uiICS->do_callback();
            if (panel::colorPanel)
                panel::colorPanel->refresh();
        }

        int ocioIcsIndex(const std::string& name)
        {
            auto uiICS = App::ui->uiICS;
            int value = -1;
            for (int i = 0; i < uiICS->children(); ++i)
            {
                const Fl_Menu_Item* item = uiICS->child(i);
                if (!item || !item->label() || item->flags & FL_SUBMENU)
                    continue;

                char pathname[1024];
                int ret = uiICS->item_pathname(pathname, 1024, item);
                if (ret != 0)
                    continue;

                if (name == pathname)
                {
                    value = i;
                    break;
                }
            }
            return value;
        }

        std::string ocioLook()
        {
            auto OCIOLook = App::ui->OCIOLook;
            int idx = OCIOLook->value();
            if (idx < 0 || idx >= OCIOLook->children())
                return "";

            const Fl_Menu_Item* item = OCIOLook->child(idx);
            return item->label();
        }

        void setOcioLook(const std::string& name)
        {
            auto OCIOLook = App::ui->OCIOLook;
            int value = -1;
            if (name.empty())
                value = 0;
            for (int i = 0; i < OCIOLook->children(); ++i)
            {
                const Fl_Menu_Item* item = OCIOLook->child(i);
                if (!item || !item->label() || item->flags & FL_SUBMENU)
                    continue;

                char pathname[1024];
                int ret = OCIOLook->item_pathname(pathname, 1024, item);
                if (ret != 0)
                    continue;

                if (name == pathname || name == item->label())
                {
                    value = i;
                    break;
                }
            }
            if (value == -1)
            {
                std::string err =
                    string::Format(_("Invalid OCIO Look '{0}'.")).arg(name);
                throw std::runtime_error(err);
                return;
            }
            OCIOLook->value(value);
            OCIOLook->do_callback();
            if (panel::colorPanel)
                panel::colorPanel->refresh();
        }

        int ocioLookIndex(const std::string& name)
        {
            auto uiLook = App::ui->OCIOLook;
            int value = -1;
            for (int i = 0; i < uiLook->children(); ++i)
            {
                const Fl_Menu_Item* item = uiLook->child(i);
                if (!item || !item->label() || item->flags & FL_SUBMENU)
                    continue;

                char pathname[1024];
                int ret = uiLook->item_pathname(pathname, 1024, item);
                if (ret != 0)
                    continue;

                if (name == pathname)
                {
                    value = i;
                    break;
                }
            }
            return value;
        }

        std::string ocioView()
        {
            auto uiOCIOView = App::ui->OCIOView;
            int idx = uiOCIOView->value();
            if (idx < 0 || idx >= uiOCIOView->children())
                return "";

            const Fl_Menu_Item* item = uiOCIOView->child(idx);

            char pathname[1024];
            int ret = uiOCIOView->item_pathname(pathname, 1024, item);
            if (ret != 0)
                return "";

            std::string view = pathname;
            if (view[0] == '/')
                view = view.substr(1, view.size());
            return view;
        }

        void setOcioView(const std::string& name)
        {
            auto uiOCIOView = App::ui->OCIOView;
            int value = -1;
            for (int i = 0; i < uiOCIOView->children(); ++i)
            {
                const Fl_Menu_Item* item = uiOCIOView->child(i);
                if (!item || !item->label() || (item->flags & FL_SUBMENU))
                    continue;

                char pathname[1024];
                int ret = uiOCIOView->item_pathname(pathname, 1024, item);
                if (ret != 0)
                    continue;

                if (name == pathname || name == item->label())
                {
                    value = i;
                    break;
                }
            }
            if (value == -1)
            {
                std::string err =
                    string::Format(_("Invalid OCIO Display/View '{0}'."))
                        .arg(name);
                throw std::runtime_error(err);
            }
            uiOCIOView->value(value);
            uiOCIOView->do_callback();
            if (panel::colorPanel)
                panel::colorPanel->refresh();
        }

        std::string ocioDisplayViewShortened(
            const std::string& display, const std::string& view)
        {
            if (view == _("None"))
                return view;
            
            std::string out;
            auto uiOCIOView = App::ui->OCIOView;
            bool has_submenu = false;
            for (int i = 0; i < uiOCIOView->children(); ++i)
            {
                const Fl_Menu_Item* item = uiOCIOView->child(i);
                if (!item)
                    continue;
                if (item->flags & FL_SUBMENU)
                {
                    has_submenu = true;
                    break;
                }
            }
            if (has_submenu)
            {
                out = display + '/' + view;
            }
            else
            {
                out = view + " (" + display + ')';
            }
            return out;
        }

        void ocioSplitViewIntoDisplayView(
            const std::string& combined, std::string& display,
            std::string& view)
        {
            view = combined;
            size_t pos = view.rfind('/');
            if (pos != std::string::npos)
            {
                display = view.substr(0, pos);
                view = view.substr(pos + 1, view.size());
            }
            else
            {
                pos = view.find('(');
                if (pos == std::string::npos)
                {
                    const std::string& err =
                        string::Format(
                            _("Could not split '{0}' into display and view."))
                            .arg(combined);
                    throw std::runtime_error(err);
                }

                display = view.substr(pos + 1, view.size());
                view = view.substr(0, pos - 1);
                pos = display.find(')');
                display = display.substr(0, pos);
            }
        }

        int ocioViewIndex(const std::string& displayViewName)
        {
            int value = -1;
            auto uiOCIOView = App::ui->OCIOView;
            for (int i = 0; i < uiOCIOView->children(); ++i)
            {
                const Fl_Menu_Item* item = uiOCIOView->child(i);
                if (!item || !item->label() || (item->flags & FL_SUBMENU))
                    continue;

                char pathname[1024];
                int ret = uiOCIOView->item_pathname(pathname, 1024, item);
                if (ret != 0)
                    continue;

                if (displayViewName == pathname)
                {
                    value = i;
                    break;
                }
            }
            return value;
        }

        std::vector<std::string> ocioIcsList()
        {
            auto uiICS = App::ui->uiICS;
            std::vector<std::string> out;
            for (int i = 0; i < uiICS->children(); ++i)
            {
                const Fl_Menu_Item* item = uiICS->child(i);
                if (!item || !item->label() || item->flags & FL_SUBMENU)
                    continue;

                char pathname[1024];
                int ret = uiICS->item_pathname(pathname, 1024, item);
                if (ret != 0)
                    continue;

                if (pathname[0] == '/')
                    out.push_back(item->label());
                else
                    out.push_back(pathname);
            }
            return out;
        }

        std::vector<std::string> ocioLookList()
        {
            auto OCIOLook = App::ui->OCIOLook;
            std::vector<std::string> out;
            for (int i = 0; i < OCIOLook->children(); ++i)
            {
                const Fl_Menu_Item* item = OCIOLook->child(i);
                if (!item || !item->label())
                    continue;

                out.push_back(item->label());
            }
            return out;
        }

        std::vector<std::string> ocioViewList()
        {
            auto uiOCIOView = App::ui->OCIOView;
            std::vector<std::string> out;
            for (int i = 0; i < uiOCIOView->children(); ++i)
            {
                const Fl_Menu_Item* item = uiOCIOView->child(i);
                if (!item || !item->label() || (item->flags & FL_SUBMENU))
                    continue;

                char pathname[1024];
                int ret = uiOCIOView->item_pathname(pathname, 1024, item);
                if (ret != 0)
                    continue;

                if (pathname[0] == '/')
                    out.push_back(item->label());
                else
                    out.push_back(pathname);
            }
            return out;
        }

        struct OCIODefaults
        {
            std::string bits8;
            std::string bits16;
            std::string bits32;
            std::string half;
            std::string flt;
        };

        struct OCIOPreset
        {
            std::string name;

            timeline::OCIOOptions ocio;
            timeline::LUTOptions lut;

            OCIODefaults defaults;
        };

        void to_json(nlohmann::json& j, const OCIODefaults& value)
        {
            j = nlohmann::json{
                {"8-bits", value.bits8},   {"16-bits", value.bits16},
                {"32-bits", value.bits32}, {"half", value.half},
                {"float", value.flt},
            };
        }

        void from_json(const nlohmann::json& j, OCIODefaults& value)
        {
            j.at("8-bits").get_to(value.bits8);
            j.at("16-bits").get_to(value.bits16);
            j.at("32-bits").get_to(value.bits32);
            j.at("half").get_to(value.half);
            j.at("float").get_to(value.flt);
        }

        void to_json(nlohmann::json& j, const OCIOPreset& value)
        {
            j = nlohmann::json{
                {"name", value.name},
                {"ocio", value.ocio},
                {"lut", value.lut},
                {"defaults", value.defaults},
            };
        }

        void from_json(const nlohmann::json& j, OCIOPreset& value)
        {
            j.at("name").get_to(value.name);
            j.at("ocio").get_to(value.ocio);
            j.at("lut").get_to(value.lut);
            j.at("defaults").get_to(value.defaults);
        }

        std::vector<OCIOPreset> ocioPresets;

        std::vector<std::string> ocioPresetsList()
        {
            std::vector<std::string> out;
            for (const auto& preset : ocioPresets)
            {
                out.push_back(preset.name);
            }
            return out;
        }

        std::string ocioPresetSummary(const std::string& presetName)
        {
            std::stringstream s;
            for (const auto& preset : ocioPresets)
            {
                if (preset.name == presetName)
                {
                    const timeline::OCIOOptions& ocio = preset.ocio;
                    const timeline::LUTOptions& lut = preset.lut;
                    const OCIODefaults& d = preset.defaults;

                    s << "OCIO:" << std::endl
                      << "\t  config: " << ocio.fileName << std::endl
                      << "\t     ICS: " << ocio.input << std::endl
                      << "\t display: " << ocio.display << std::endl
                      << "\t    view: " << ocio.view << std::endl
                      << "\t    look: " << ocio.look << std::endl
                      << "LUT:" << std::endl
                      << "\tfileName: " << lut.fileName << std::endl
                      << "\t   order: " << lut.order << std::endl
                      << "Defaults:" << std::endl
                      << "\t  8-bits: " << d.bits8 << std::endl
                      << "\t 16-bits: " << d.bits16 << std::endl
                      << "\t 32-bits: " << d.bits32 << std::endl
                      << "\t    half: " << d.half << std::endl
                      << "\t   float: " << d.flt << std::endl;
                }
            }
            return s.str();
        }

        void setOcioPreset(const std::string& presetName)
        {
            for (const auto& preset : ocioPresets)
            {
                if (preset.name == presetName)
                {
                    std::string msg =
                        string::Format(_("Setting OCIO Preset '{0}'."))
                            .arg(presetName);
                    LOG_INFO(msg);

                    const timeline::OCIOOptions& ocio = preset.ocio;
                    setOcioConfig(ocio.fileName);
                    setOcioIcs(ocio.input);
                    std::string view =
                        ocioDisplayViewShortened(ocio.display, ocio.view);
                    setOcioView(view);
                    setOcioLook(ocio.look);

                    App::app->setLUTOptions(preset.lut);
                    return;
                }
            }

            std::string msg =
                string::Format(_("Preset '{0}' not found.")).arg(presetName);
            LOG_ERROR(msg);
        }

        void createOcioPreset(const std::string& presetName)
        {
            for (const auto& preset : ocioPresets)
            {
                if (preset.name == presetName)
                {
                    std::string msg =
                        string::Format(_("OCIO Preset '{0}' already exists!"))
                            .arg(presetName);
                    LOG_ERROR(msg);
                    return;
                }
            }

            auto uiPrefs = App::ui->uiPrefs;

            timeline::OCIOOptions ocio;
            ocio.enabled = true;

            ocio.fileName = ocioConfig();
            ocio.input = ocioIcs();

            std::string display, view;
            std::string combined = ocioView();
            ocioSplitViewIntoDisplayView(combined, display, view);

            ocio.display = display;
            ocio.view = view;
            ocio.look = ocioLook();

            const timeline::LUTOptions& lut = App::app->lutOptions();

            OCIODefaults defaults;
            defaults.bits8 = uiPrefs->uiOCIO_8bits_ics->value();
            defaults.bits16 = uiPrefs->uiOCIO_16bits_ics->value();
            defaults.bits32 = uiPrefs->uiOCIO_32bits_ics->value();
            defaults.half = uiPrefs->uiOCIO_half_ics->value();
            defaults.flt = uiPrefs->uiOCIO_float_ics->value();

            OCIOPreset preset;
            preset.name = presetName;
            preset.ocio = ocio;
            preset.lut = lut;
            preset.defaults = defaults;

            ocioPresets.push_back(preset);
        }

        void removeOcioPreset(const std::string& presetName)
        {
            std::vector<OCIOPreset> out;
            bool found = false;
            for (const auto& preset : ocioPresets)
            {
                if (preset.name == presetName)
                {
                    found = true;
                    continue;
                }
                out.push_back(preset);
            }
            if (!found)
            {
                std::string msg = string::Format(_("Preset '{0}' not found."))
                                      .arg(presetName);
                LOG_ERROR(msg);
            }
            ocioPresets = out;
        }

        bool loadOcioPresets(const std::string& fileName)
        {
            try
            {
                std::ifstream ifs(fileName);
                if (!ifs.is_open())
                {
                    const std::string& err =
                        string::Format(
                            _("Failed to open the file '{0}' for reading."))
                            .arg(fileName);
                    LOG_ERROR(err);
                    return false;
                }

                nlohmann::json j;
                ifs >> j;

                if (ifs.fail())
                {
                    const std::string& err =
                        string::Format(_("Failed to load the file '{0}'."))
                            .arg(fileName);
                    LOG_ERROR(err);
                    return false;
                }
                if (ifs.bad())
                {
                    LOG_ERROR(
                        _("The stream is in an unrecoverable error state."));
                    return false;
                }
                ifs.close();

                ocioPresets = j.get<std::vector<OCIOPreset>>();

                const std::string& msg =
                    string::Format(_("Loaded {0} ocio presets from \"{1}\"."))
                        .arg(ocioPresets.size())
                        .arg(fileName);
                LOG_INFO(msg);
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("Error: " << e.what());
            }
            return true;
        }

        bool saveOcioPresets(const std::string& fileName)
        {
            try
            {
                std::ofstream ofs(fileName);
                if (!ofs.is_open())
                {
                    const std::string& err =
                        string::Format(
                            _("Failed to open the file '{0}' for saving."))
                            .arg(fileName);
                    LOG_ERROR(err);
                    return false;
                }

                nlohmann::json j = ocioPresets;

                ofs << j.dump(4);

                if (ofs.fail())
                {
                    const std::string& err =
                        string::Format(_("Failed to save the file '{0}'."))
                            .arg(fileName);
                    LOG_ERROR(err);
                    return false;
                }
                if (ofs.bad())
                {
                    LOG_ERROR(
                        _("The stream is in an unrecoverable error state."));
                    return false;
                }
                ofs.close();

                const std::string& msg =
                    string::Format(
                        _("OCIO presets have been saved to \"{0}\"."))
                        .arg(fileName);
                LOG_INFO(msg);
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("Error: " << e.what());
            }
            return true;
        }
    } // namespace ocio
} // namespace mrv
