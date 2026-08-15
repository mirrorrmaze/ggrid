#include "IRLibrary.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace GGrid::IRLibrary
{
    const std::vector<Entry>& getCatalog()
    {
        static const std::vector<Entry> combined = []
        {
            std::vector<Entry> catalog = {
            // Residential
            { "Arroyo House Living Room",     "Residential", "Residential/Arroyo House Living Room.wav" },
            { "College House Bedroom",        "Residential", "Residential/College House Bedroom.wav" },
            { "College House Master Bathroom","Residential", "Residential/College House Master Bathroom.wav" },
            { "Old Home Living Room",         "Residential", "Residential/Old Home Living Room.wav" },
            { "Ranch House Bathroom",         "Residential", "Residential/Ranch House Bathroom.wav" },
            { "Colonial House Bedroom",       "Residential", "Residential/Colonial House Bedroom.wav" },

            // Commercial
            { "Royal Ballroom",               "Commercial", "Commercial/Royal Ballroom.wav" },
            { "Lux Hotel Bathroom",           "Commercial", "Commercial/Lux Hotel Bathroom.wav" },
            { "Firenze Parking Garage",       "Commercial", "Commercial/Firenze Parking Garage.wav" },
            { "Craft Coffee Shop",            "Commercial", "Commercial/Craft Coffee Shop.wav" },
            { "Tall Coffee Shop Lobby",       "Commercial", "Commercial/Tall Coffee Shop Lobby.wav" },

            // Public
            { "Giant Center Arena",           "Public", "Public/Giant Center Arena.wav" },
            { "UCF Engineering Atrium",       "Public", "Public/UCF Engineering Atrium.wav" },
            { "UCF Audio Club Room",          "Public", "Public/UCF Audio Club Room.wav" },
            { "Phoenix Convention Center Hall","Public", "Public/Phoenix Convention Center Hall.wav" },

            // Historical
            { "The Pantheon (Rome)",          "Historical", "Historical/The Pantheon Rome.wav" },
            { "Drumheller's Little Church",   "Historical", "Historical/Drumhellers Little Church.wav" },
            { "Sunnybrook Willowdale Church", "Historical", "Historical/Sunnybrook Willowdale Church.wav" },
            { "Menorcan Civil War Bunker",    "Historical", "Historical/Menorcan Civil War Bunker.wav" },

            // Textures (bonus, non-room)
            { "HG Spring",                    "Textures", "Textures/HG Spring.wav" },
            { "Letter Holding Coil Spring",   "Textures", "Textures/Letter Holding Coil Spring.wav" },
            { "Conner Plate Impact",          "Textures", "Textures/Conner Plate Impact.wav" },

            // --- Second batch (appended -- do not reorder, indices above are load-bearing for
            // saved presets/automation on slot{N}_convolution_irIndex) ---

            // Residential
            { "13th Century Venetian Home",   "Residential", "Residential/13th Century Venetian Home.wav" },
            { "Villa 10 Hall",                "Residential", "Residential/Villa 10 Hall.wav" },
            { "Old Home Fireplace",           "Residential", "Residential/Old Home Fireplace.wav" },
            { "Ranch House Hall",             "Residential", "Residential/Ranch House Hall.wav" },

            // Commercial
            { "Palm Ballroom",                "Commercial", "Commercial/Palm Ballroom.wav" },
            { "Reflective Half Bathroom",     "Commercial", "Commercial/Reflective Half Bathroom.wav" },
            { "Hotel Guest Room",             "Commercial", "Commercial/Hotel Guest Room.wav" },
            { "Ancient Wand Shop",            "Commercial", "Commercial/Ancient Wand Shop.wav" },

            // Public
            { "MSU Arena Court Seating",      "Public", "Public/MSU Arena Court Seating.wav" },
            { "Fort Worth Convention Arena",  "Public", "Public/Fort Worth Convention Arena.wav" },
            { "OCCC Freight Elevator",        "Public", "Public/OCCC Freight Elevator.wav" },
            { "Weston's Backrooms Hall",      "Public", "Public/Westons Backrooms Hall.wav" },

            // Historical
            { "Menorcan Bunker Stairwell",    "Historical", "Historical/Menorcan Bunker Stairwell.wav" },

            // Outdoors (new category)
            { "Ostia Antica Theatre",         "Outdoors", "Outdoors/Ostia Antica Theatre.wav" },
            { "Forest Clearing",              "Outdoors", "Outdoors/Forest Clearing.wav" },
            { "Rockefeller Gardens Field Stage","Outdoors", "Outdoors/Rockefeller Gardens Field Stage.wav" },
            { "Bridge Underpass",             "Outdoors", "Outdoors/Bridge Underpass.wav" },
            { "Suburb Street",                "Outdoors", "Outdoors/Suburb Street.wav" },
            { "University Garage Stairwell",  "Outdoors", "Outdoors/University Garage Stairwell.wav" },
            { "Eerie Loading Dock",           "Outdoors", "Outdoors/Eerie Loading Dock.wav" },
            { "Rome Apartment Alley",         "Outdoors", "Outdoors/Rome Apartment Alley.wav" },

            // Textures (bonus, non-room)
            { "Car Interior",                 "Textures", "Textures/Car Interior.wav" },
            { "Storm Drain",                  "Textures", "Textures/Storm Drain.wav" },
            { "Catamaran Hull",               "Textures", "Textures/Catamaran Hull.wav" },

            // --- Third batch (appended -- do not reorder, indices above are load-bearing for
            // saved presets/automation on slot{N}_convolution_irIndex) ---

            // Residential
            { "College House Foyer Stairwell","Residential", "Residential/College House Foyer Stairwell.wav" },
            { "College House Master Bedroom", "Residential", "Residential/College House Master Bedroom.wav" },
            { "College House Office",         "Residential", "Residential/College House Office.wav" },
            { "College House Living Room",    "Residential", "Residential/College House Living Room.wav" },
            { "Unknown House Lobby",          "Residential", "Residential/Unknown House Lobby.wav" },
            { "Old Home Guest Bath",          "Residential", "Residential/Old Home Guest Bath.wav" },
            { "Old Home Hallway",             "Residential", "Residential/Old Home Hallway.wav" },
            { "Ranch House Bedroom",          "Residential", "Residential/Ranch House Bedroom.wav" },
            { "Ranch House Living Room",      "Residential", "Residential/Ranch House Living Room.wav" },
            { "Colonial Bedroom (Mid Treatment)","Residential", "Residential/Colonial Bedroom Mid Treatment.wav" },

            // Commercial
            { "Orlando Ballroom",             "Commercial", "Commercial/Orlando Ballroom.wav" },
            { "Gold Coffee Shop Lobby",       "Commercial", "Commercial/Gold Coffee Shop Lobby.wav" },
            { "Gold Coffee Shop Quiet Room",  "Commercial", "Commercial/Gold Coffee Shop Quiet Room.wav" },
            { "Tall Coffee Shop Distant",     "Commercial", "Commercial/Tall Coffee Shop Distant.wav" },

            // Public
            { "MSU Arena Upper Bleachers",    "Public", "Public/MSU Arena Upper Bleachers.wav" },
            { "MSB Brick Stairwell",          "Public", "Public/MSB Brick Stairwell.wav" },
            { "PCC Prefunction Hall",         "Public", "Public/PCC Prefunction Hall.wav" },
            { "OCCC Theater Storage Barrier", "Public", "Public/OCCC Theater Storage Barrier.wav" },

            // Historical
            { "Menorcan Bunker 2 Entry",      "Historical", "Historical/Menorcan Bunker 2 Entry.wav" },
            { "Menorcan Bunker 2 Stairwell",  "Historical", "Historical/Menorcan Bunker 2 Stairwell.wav" },

            // Outdoors
            { "Gas Station Overhang",         "Outdoors", "Outdoors/Gas Station Overhang.wav" },
            { "Angled Loading Dock",          "Outdoors", "Outdoors/Angled Loading Dock.wav" },
            { "Flat Loading Dock",            "Outdoors", "Outdoors/Flat Loading Dock.wav" },
            { "Theme Park Employee Garage",   "Outdoors", "Outdoors/Theme Park Employee Garage.wav" },
            { "Bower Ponds Amphitheatre Stage","Outdoors", "Outdoors/Bower Ponds Amphitheatre Stage.wav" },
            { "University Garage Ramp",       "Outdoors", "Outdoors/University Garage Ramp.wav" },
            { "Medium Gazebo",                "Outdoors", "Outdoors/Medium Gazebo.wav" },
            { "Playground Slide",             "Outdoors", "Outdoors/Playground Slide.wav" },
            { "PCC North Terrace",            "Outdoors", "Outdoors/PCC North Terrace.wav" },
            { "Suburban Apartment Avenue",    "Outdoors", "Outdoors/Suburban Apartment Avenue.wav" },
            { "Old Home Backyard",            "Outdoors", "Outdoors/Old Home Backyard.wav" },

            // Textures (bonus, non-room) -- plates
            { "Conner Plate Short",           "Textures", "Textures/Conner Plate Short.wav" },
            { "Conner Plate Medium",          "Textures", "Textures/Conner Plate Medium.wav" },
            { "Conner Plate Long",            "Textures", "Textures/Conner Plate Long.wav" },
            { "Conner Plate Huge",            "Textures", "Textures/Conner Plate Huge.wav" },

            // Textures (bonus, non-room) -- springs
            { "Amateur Speaker Spring",       "Textures", "Textures/Amateur Speaker Spring.wav" },
            { "Boom Arm Spring",              "Textures", "Textures/Boom Arm Spring.wav" },
            { "Reverb Amp 500 Spring",        "Textures", "Textures/Reverb Amp 500 Spring.wav" },
            { "Space Heater Spring Tank",     "Textures", "Textures/Space Heater Spring Tank.wav" },
            { "Spring Tube Toy",              "Textures", "Textures/Spring Tube Toy.wav" },

            // Textures (bonus, non-room) -- found objects
            { "Basilica Dome Glass Bowls",    "Textures", "Textures/Basilica Dome Glass Bowls.wav" },
            { "Conch Shell Sweep",            "Textures", "Textures/Conch Shell Sweep.wav" },
            { "Rusty Steel Sheet",            "Textures", "Textures/Rusty Steel Sheet.wav" },
            { "Nitro Cold Brew Keg",          "Textures", "Textures/Nitro Cold Brew Keg.wav" },
            { "Broadsword Contact",           "Textures", "Textures/Broadsword Contact.wav" },
            { "Toilet Bowl",                  "Textures", "Textures/Toilet Bowl.wav" },
            { "Propane Tank Knuckle",         "Textures", "Textures/Propane Tank Knuckle.wav" },

            // Textures (bonus, non-room) -- enclosed spaces
            { "Old Home Oven",                "Textures", "Textures/Old Home Oven.wav" },
            { "2002 Sedan Interior",          "Textures", "Textures/2002 Sedan Interior.wav" },
            };

            jassert ((int) catalog.size() == factoryCatalogSize);

            // Custom IRs: anything the user has dropped into Custom/ (searched recursively, so
            // they can organize their own files into subfolders if they want), appended after
            // the factory list and sorted by path for a stable order within a session -- see
            // getCatalog()'s header comment for why that's "stable within a session" and not a
            // permanent guarantee the way the factory indices are.
            auto customDir = getCustomIRDirectory();
            if (customDir.isDirectory())
            {
                juce::AudioFormatManager formatManager;
                formatManager.registerBasicFormats();

                juce::Array<juce::File> files;
                customDir.findChildFiles (files, juce::File::findFiles, true);
                files.sort();

                for (auto& f : files)
                {
                    if (formatManager.findFormatForFileExtension (f.getFileExtension()) == nullptr)
                        continue;

                    Entry e;
                    e.displayName = f.getFileNameWithoutExtension();
                    e.category = "Custom";
                    e.relativePath = "Custom/" + f.getRelativePathFrom (customDir).replaceCharacter ('\\', '/');
                    catalog.push_back (e);
                }
            }

            return catalog;
        }();

        return combined;
    }

    juce::StringArray getCatalogDisplayNames()
    {
        juce::StringArray names;
        for (auto& entry : getCatalog())
            names.add (entry.displayName);
        return names;
    }

    juce::File getCustomIRDirectory()
    {
        auto root = resolveIRRoot();
        if (! root.isDirectory())
            return {};

        auto customDir = root.getChildFile ("Custom");
        if (! customDir.isDirectory())
            customDir.createDirectory();

        return customDir;
    }

    juce::File resolveIRRoot()
    {
        // 1) Next to the running binary (plugin or standalone), under Resources/IRs -- this is
        //    where the installer places factory content.
        auto exeDir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
        auto besideExe = exeDir.getChildFile ("Resources").getChildFile ("IRs");
        if (besideExe.isDirectory())
            return besideExe;

        // 2) Dev-build fallback: walk up from the executable looking for the source tree's
        //    Resources/IRs (works for both the Standalone app and the VST3, wherever CMake puts them).
        auto probe = exeDir;
        for (int i = 0; i < 8 && probe.exists(); ++i)
        {
            auto candidate = probe.getChildFile ("Resources").getChildFile ("IRs");
            if (candidate.isDirectory())
                return candidate;
            probe = probe.getParentDirectory();
        }

        // 3) User content-pack location -- Documents\GGrid\IRs, independent of both the
        //    Standalone's install dir and the VST3's (which may differ per user, and which a
        //    plugin can't always resolve reliably when hosted).
        auto userDir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                           .getChildFile ("GGrid").getChildFile ("IRs");
        if (userDir.isDirectory())
            return userDir;

        return {};
    }

    bool loadEntry (int index, double targetSampleRate, juce::AudioBuffer<float>& outBuffer)
    {
        outBuffer.setSize (0, 0);

        const auto& catalog = getCatalog();
        if (index < 0 || index >= (int) catalog.size())
            return false;

        auto root = resolveIRRoot();
        if (! root.isDirectory())
            return false;

        auto file = root.getChildFile (catalog[(size_t) index].relativePath);
        if (! file.existsAsFile())
            return false;

        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();

        std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
        if (reader == nullptr)
            return false;

        const int numChannels = (int) reader->numChannels;
        const int numSamples = (int) reader->lengthInSamples;
        if (numChannels <= 0 || numSamples <= 0)
            return false;

        juce::AudioBuffer<float> raw (numChannels, numSamples);
        reader->read (&raw, 0, numSamples, 0, true, true);

        if (std::abs (reader->sampleRate - targetSampleRate) < 0.5)
        {
            outBuffer = std::move (raw);
            return true;
        }

        // Resample to the host's current sample rate.
        const double ratio = reader->sampleRate / targetSampleRate;
        const int resampledLength = (int) std::ceil ((double) numSamples / ratio) + 1;
        outBuffer.setSize (numChannels, resampledLength);

        // LagrangeInterpolator::process requires the input to hold at least (ratio *
        // resampledLength) samples -- resampledLength is deliberately rounded up by 1 extra
        // output sample here, which on its own already asks for more input than `raw` strictly
        // has, before any floating-point drift or the interpolator's own lookahead pushes it
        // further past that boundary. Left unpadded this reads past raw's allocation into
        // whatever heap memory follows it (see IRProcessor::buildShapedIR's Stretch resample for
        // the same issue, where it was actually observed as tail noise).
        constexpr int interpolatorReadPadding = 16;
        juce::AudioBuffer<float> paddedRaw (numChannels, numSamples + interpolatorReadPadding);
        paddedRaw.clear();
        for (int ch = 0; ch < numChannels; ++ch)
            paddedRaw.copyFrom (ch, 0, raw, ch, 0, numSamples);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            juce::LagrangeInterpolator interpolator;
            const int written = interpolator.process (ratio,
                                                        paddedRaw.getReadPointer (ch),
                                                        outBuffer.getWritePointer (ch),
                                                        resampledLength);
            outBuffer.setSize (numChannels, written, true, false, true);
        }

        return true;
    }
}
