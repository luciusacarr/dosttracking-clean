/**
 * LOST starting point
 *
 * Reads in CLI arguments/flags and starts the appropriate pipelines
 */

#include <assert.h>
#include <sys/types.h>
#include <unistd.h>
#include <getopt.h>

#include <bitset>
#include <string>
#include <iostream>
#include <fstream>
#include <chrono>
#include <cstring>
#include <map>
#include <deque>


#include "databases.hpp"
#include "centroiders.hpp"
#include "decimal.hpp"
#include "io.hpp"
#include "man-database.h"
#include "man-pipeline.h"
#include "star-id.hpp"
#include "star-utils.hpp"

#include <dirent.h>



#include <SFML/Graphics.hpp>

#include <sfml-utils.hpp>





namespace lost {

/// Create a database and write it to a file based on the command line options in \p values
static void DatabaseBuild(const DatabaseOptions &values) {
    Catalog narrowedCatalog = NarrowCatalog(CatalogRead(), (int) (values.minMag * 100), values.maxStars, DegToRad(values.minSeparation));
    std::cerr << "Narrowed catalog has " << narrowedCatalog.size() << " stars." << std::endl;

    MultiDatabaseDescriptor dbEntries = GenerateDatabases(narrowedCatalog, values);
    SerializeContext ser = serFromDbValues(values);

    // Create & Set Flags.
    uint32_t dbFlags = 0;
    dbFlags |= typeid(decimal) == typeid(float) ? MULTI_DB_FLOAT_FLAG : 0;

    // Serialize Flags
    SerializeMultiDatabase(&ser, dbEntries, dbFlags);

    std::cerr << "Generated database with " << ser.buffer.size() << " bytes" << std::endl;
    std::cerr << "Database flagged with " << std::bitset<8*sizeof(dbFlags)>(dbFlags) << std::endl;

    UserSpecifiedOutputStream pos = UserSpecifiedOutputStream(values.outputPath, true);
    pos.Stream().write((char *) ser.buffer.data(), ser.buffer.size());

}

/// Run a star-tracking pipeline (possibly including generating inputs and analyzing outputs) based on command line options in \p values.
static void PipelineRun(const PipelineOptions &values) {
    Pipeline pipeline = SetPipeline(values);

    if (values.imageDir != "") {
        std::vector<std::string> validFiles = GetImagesInDirectory(values.imageDir);

        if (validFiles.size() == 0) {
            std::cerr << "No valid PNG files found in directory " << values.imageDir << "... terminating." << std::endl;
            exit(1);
        }

        for (const std::string& filename : validFiles) {
            PipelineOptions workingValues = values;

            workingValues.png = workingValues.imageDir + "/" + filename;

            PipelineInputList input = GetPipelineInput(workingValues);
            
            std::vector<PipelineOutput> outputs = pipeline.Go(input);
            PipelineComparison(input, outputs, values);
        }
    } else {
        PipelineInputList input = GetPipelineInput(values);
        std::vector<PipelineOutput> outputs = pipeline.Go(input);

        PipelineComparison(input, outputs, values);
    }
}

static std::vector<dost_ImgData> PipelineRunSFML(PipelineOptions &values) {
    std::vector<dost_ImgData> returnData;
    values.centroidAlgo = "cog";
    values.idAlgo = "py";
    values.attitudeAlgo = "dqm";
    values.databasePath = "my-database.dat";

    Pipeline pipeline = SetPipeline(values);

    if (values.imageDir != "") {
        DIR *dir; struct dirent *ent;
        std::vector<std::string> validFiles;
        if ((dir = opendir(values.imageDir.c_str())) != NULL) {
            while ((ent = readdir(dir)) != NULL) {
                std::string filename = ent->d_name;
                if (filename.length() >= 4 && filename.substr(filename.length() - 4) == ".png")
                    validFiles.push_back(filename);
            }
            closedir(dir);
        }
        std::sort(validFiles.begin(), validFiles.end());

        for (const std::string& filename : validFiles) {
            values.png = values.imageDir + "/" + filename;
            PipelineInputList input = GetPipelineInput(values);
            std::vector<PipelineOutput> outputs = pipeline.Go(input);

            if (!outputs.empty()) {
                const auto& out = outputs[0];
                dost_ImgData imgData;
                if (out.attitude) imgData.attitude = *out.attitude;
                if (out.stars) imgData.stars = *out.stars;
                imgData.resx = input[0]->InputImage()->width;
                imgData.resy = input[0]->InputImage()->height;
                imgData.trackedStars = out.trackedStars;

                // Parse Truth Data
                imgData.trueRa = imgData.trueDec = imgData.trueRoll = 0;
                std::string txtPath = values.imageDir + "/" + filename.substr(0, filename.find_last_of(".")) + ".txt";
                std::ifstream txtFile(txtPath);
                if (txtFile.is_open()) {
                    std::string line, key; double val; bool known = false;
                    while (std::getline(txtFile, line)) {
                        std::stringstream ss(line);
                        if (ss >> key >> val) {
                            if (key == "attitude_known" && val == 1.0) known = true;
                            else if (key == "attitude_ra") imgData.trueRa = val;
                            else if (key == "attitude_de") imgData.trueDec = val;
                            else if (key == "attitude_roll") imgData.trueRoll = val;
                        }
                    }
                }
                returnData.push_back(imgData);
            }
        }
    } else {

        // generate mode, standard!

        values.generate = 1;

        if (values.frames < 1) values.frames = 1;
        if (values.rollMax == 0) values.rollMax = values.rollMin;
        if (values.raMax == 0)   values.raMax   = values.raMin;
        if (values.decMax == 0)  values.decMax  = values.decMin;

        

        if (values.regenFalseDb) {
            std::cout << "Regenerating fake star database..." << std::endl;
            UpdateFakeStarsFile("fakestars.tsv", values.generateFalseMinMag, values.generateFalseMaxMag);
        }

        returnData.reserve(values.panning ? 1 : values.frames);

        int startFrame = 0;
        if (values.panning) startFrame = values.frames - 1;

        for (int frame = startFrame; frame < values.frames; frame++) {
            std::cout << "Processing frame: " << frame << "\n";

            double t = (values.frames > 1) ? (double)frame / (values.frames - 1) : 0.0;

            values.generateRoll = values.rollMin + t * (values.rollMax - values.rollMin);
            values.generateRa   = values.raMin   + t * (values.raMax   - values.raMin);
            values.generateDe   = values.decMin  + t * (values.decMax  - values.decMin);

            char buffer[256];
            snprintf(buffer, sizeof(buffer), "sfml-tests/frame_%04d.png", frame);
            values.plotRawInput = std::string(buffer);

            PipelineInputList input = GetPipelineInput(values);
            std::vector<PipelineOutput> outputs = pipeline.Go(input);

            if (outputs.empty()) continue;

            const auto& out = outputs[0];
            dost_ImgData imgData;
            
            if (out.attitude) imgData.attitude = *out.attitude;
            if (out.stars)    imgData.stars    = *out.stars;

            if (out.starIds && !out.catalog.empty()) {
                for (const StarIdentifier &id : *out.starIds) {
                    imgData.starIds.emplace_back(id.starIndex, id.catalogIndex);
                }
            }

            imgData.trueRa   = values.generateRa;
            imgData.trueDec  = values.generateDe;
            imgData.trueRoll = values.generateRoll;

            imgData.resx = values.generateXRes;
            imgData.resy = values.generateYRes;

            imgData.trackedStars = out.trackedStars;

            returnData.push_back(imgData);
            PipelineComparison(input, outputs, values); 
        }
    }

    return returnData;
}

// DO NOT DELETE
// static void PipelineBenchmark() {
//     PipelineInputList input = PromptPipelineInput();
//     Pipeline pipeline = PromptPipeline();
//     int iterations = Prompt<int>("Times to run the pipeline");
//     std::cerr << "Benchmarking..." << std::endl;

//     // TODO: we can do better than this :| maybe include mean time, 99% time, or allow a vector of
//     // input and determine which one took the longest
//     auto startTime = std::chrono::high_resolution_clock::now();
//     for (int i = 0; i < iterations; i++) {
//         pipeline.Go(input);
//     }
//     auto endTime = std::chrono::high_resolution_clock::now();
//     auto totalTime = std::chrono::duration<double, std::milli>(endTime - startTime);
//     std::cout << "total_ms " << totalTime.count() << std::endl;
// }

// static void EstimateCamera() {
//     std::cerr << "Enter estimated camera details when prompted." << std::endl;
//     PipelineInputList inputs = PromptPngPipelineInput();
//     float baseFocalLength = inputs[0]->InputCamera()->FocalLength();
//     float deviationIncrement = Prompt<float>("Focal length increment (base: " + std::to_string(baseFocalLength) + ")");
//     float deviationMax = Prompt<float>("Maximum focal length deviation to attempt");
//     Pipeline pipeline = PromptPipeline();

//     while (inputs[0]->InputCamera()->FocalLength() - baseFocalLength <= deviationMax) {
//         std::cerr << "Attempt focal length " << inputs[0]->InputCamera()->FocalLength() << std::endl;
//         std::vector<PipelineOutput> outputs = pipeline.Go(inputs);
//         if (outputs[0].nice) {
//             std::cout << "camera_identified true" << std::endl << *inputs[0]->InputCamera();
//             return;
//         }

//         Camera camera(*inputs[0]->InputCamera());
//         if (camera.FocalLength() - baseFocalLength > 0) {
//             // yes i know this expression can be simplified shut up
//             camera.SetFocalLength(camera.FocalLength() - 2*(camera.FocalLength() - baseFocalLength));
//         } else {
//             camera.SetFocalLength(camera.FocalLength() + 2*(baseFocalLength - camera.FocalLength()) + deviationIncrement);
//         }
//         ((PngPipelineInput *)(inputs[0].get()))->SetCamera(camera);
//     }
//     std::cout << "camera_identified false" << std::endl;
// }

/// Convert string to boolean
bool atobool(const char *cstr) {
    std::string str(cstr);
    if (str == "1" || str == "true") {
        return true;
    }
    if (str == "0" || str == "false") {
        return false;
    }
    assert(false);
}

struct TextureInfo {
    sf::Texture texture;
    float scaleFactor; // 1.0 = Original, 0.5 = Half Size, etc.
};

TextureInfo LoadTextureSafe(const std::string& filepath) {
    sf::Image rawImage;
    if (!rawImage.loadFromFile(filepath)) {
        std::cerr << "Error: File not found: " << filepath << std::endl;
        return {sf::Texture(), 1.0f};
    }

    unsigned int maxDim = sf::Texture::getMaximumSize(); // e.g. 4096
    sf::Vector2u imgSize = rawImage.getSize();

    // If image is within limits, load normally
    if (imgSize.x <= maxDim && imgSize.y <= maxDim) {
        sf::Texture tex;
        tex.loadFromImage(rawImage);
        return {tex, 1.0f};
    }

    // --- DOWNSCALING LOGIC ---
    // Calculate how much we need to shrink to fit GPU
    float scale = (float)maxDim / std::max(imgSize.x, imgSize.y);
    unsigned int newW = (unsigned int)(imgSize.x * scale);
    unsigned int newH = (unsigned int)(imgSize.y * scale);
    
    std::cout << "[GPU LIMIT] Downscaling " << imgSize.x << "x" << imgSize.y 
              << " -> " << newW << "x" << newH << " (Scale: " << scale << ")" << std::endl;

    // Create resized image (Nearest Neighbor is fast & preserves star crispness)
    sf::Image resized;
    resized.create(newW, newH);
    const uint8_t* srcPixels = rawImage.getPixelsPtr();
    
    // Simple pixel mapping loop
    for (unsigned int y = 0; y < newH; y++) {
        for (unsigned int x = 0; x < newW; x++) {
            int srcX = (int)(x / scale);
            int srcY = (int)(y / scale);
            resized.setPixel(x, y, rawImage.getPixel(srcX, srcY));
        }
    }

    sf::Texture tex;
    tex.loadFromImage(resized);
    return {tex, scale};
}


    // Standard rotation for Celestial Attitude
    static Vec3 fromAttitude(float raDeg, float decDeg, float rollDeg, Vec3 localAxis) {
        float ra = raDeg * M_PI / 180.0f;
        float dec = decDeg * M_PI / 180.0f;
        float roll = rollDeg * M_PI / 180.0f;

        // 1. Roll (around Z)
        float x1 = localAxis.x * cos(roll) - localAxis.y * sin(roll);
        float y1 = localAxis.x * sin(roll) + localAxis.y * cos(roll);
        float z1 = localAxis.z;

        // 2. Declination (around X)
        float x2 = x1;
        float y2 = y1 * cos(dec) + z1 * sin(dec);  // Changed to +
        float z2 = -y1 * sin(dec) + z1 * cos(dec); // Changed to -
        
        // 3. RA (around Y)
        float x3 = x2 * cos(ra) + z2 * sin(ra);
        float y3 = y2;
        float z3 = -x2 * sin(ra) + z2 * cos(ra);

        return {x3, y3, z3};
    }


void DrawGimbal(sf::RenderWindow& window, float ra, float dec, float roll, sf::Vector2f center, std::string label, sf::Font& font, sf::Color color) {
    float size = 40.0f; // Radius of the sphere HUD

    auto project = [&](Vec3 point) {
        // center is captured from DrawGimbal's arguments
        // -point.y because SFML's Y axis points down
        return center + sf::Vector2f(point.x, -point.y);
    };

    // Define local unit vectors
    Vec3 localForward = {0, 0, 1};
    Vec3 localUp = {0, 1, 0};
    Vec3 localRight = {1, 0, 0};

    // Transform them to "World" 3D space
    Vec3 f = fromAttitude(ra, dec, roll, localForward);
    Vec3 u = fromAttitude(ra, dec, roll, localUp);
    Vec3 r = fromAttitude(ra, dec, roll, localRight);

    // Helper to draw a 3D vector as a 2D line (ignoring Z for projection)
    auto drawAxis = [&](Vec3 v, sf::Color c, float thickness, bool isLookVector) {
        sf::Vector2f endPoint = center + sf::Vector2f(v.x * size, -v.y * size);
                
        // Draw the main line
        sf::Vertex line[] = {
            sf::Vertex(center, c),
            sf::Vertex(endPoint, c)
        };
        window.draw(line, 2, sf::Lines);

        // If it's the look vector, add a "pointer" or crosshair at the tip
        if (isLookVector) {
            float hw = 12.0f / 2.0f; // Half-width
            float hh = 8.0f / 2.0f;  // Half-height
            Vec3 lookTip = {v.x * size, v.y * size, v.z * size};

            // Helper to manually offset the tip using Right and Up vectors
            auto getCorner = [&](float rScale, float uScale) {
                return Vec3{
                    lookTip.x + (r.x * rScale) + (u.x * uScale),
                    lookTip.y + (r.y * rScale) + (u.y * uScale),
                    lookTip.z + (r.z * rScale) + (u.z * uScale)
                };
            };

            // Project the 4 corners of the "viewfinder"
            sf::Vector2f tl = project(getCorner(-hw,  hh)); // Top Left
            sf::Vector2f tr = project(getCorner( hw,  hh)); // Top Right
            sf::Vector2f bl = project(getCorner(-hw, -hh)); // Bottom Left
            sf::Vector2f br = project(getCorner( hw, -hh)); // Bottom Right

            // Draw the frame lines
            sf::Vertex frameLines[] = {
                sf::Vertex(tl, c), sf::Vertex(tr, c),
                sf::Vertex(tr, c), sf::Vertex(br, c),
                sf::Vertex(br, c), sf::Vertex(bl, c),
                sf::Vertex(bl, c), sf::Vertex(tl, c)
            };
            window.draw(frameLines, 8, sf::Lines);
            

        }
    };



    // Draw background sphere silhouette
    sf::CircleShape sphere(size);
    sphere.setOrigin(size, size);
    sphere.setPosition(center);
    sphere.setFillColor(sf::Color(255, 255, 255, 20)); // Faint transparent white
    sphere.setOutlineColor(sf::Color(255, 255, 255, 50));
    sphere.setOutlineThickness(1);
    window.draw(sphere);


    drawAxis(r, sf::Color(255, 0, 0, 150), 1.0f, false);   // Right (Red)
    drawAxis(u, sf::Color(0, 255, 0, 150), 1.0f, false);   // Up (Green)

    drawAxis(f, color, 3.0f, true); // Highlight color (Cyan/White)

    // Label
    sf::Text txt(label, font, 10);



    sf::FloatRect textBounds = txt.getLocalBounds();
    txt.setOrigin(textBounds.left + textBounds.width / 2.0f, 0); 


    txt.setPosition(center.x, center.y + size + 10);
    txt.setFillColor(color);
    window.draw(txt);
}

/**
 * Handle optional CLI arguments
 * https://stackoverflow.com/a/69177115
 */
#define LOST_OPTIONAL_OPTARG()                                   \
    ((optarg == NULL && optind < argc && argv[optind][0] != '-') \
     ? (bool) (optarg = argv[optind++])                          \
     : (optarg != NULL))

// This is separate from `main` just because it's in the `lost` namespace
static int LostMain(int argc, char **argv) {

    if (argc == 1) {
        std::cout << "Usage: ./lost database or ./lost pipeline" << std::endl
                  << "Use --help flag on those commands for further help" << std::endl;
        return 0;
    }

    std::string command(argv[1]);
    optind = 2;

    if (command == "database") {

        enum class DatabaseCliOption {
#define LOST_CLI_OPTION(name, type, prop, defaultVal, converter, defaultArg) prop,
#include "database-options.hpp"
#undef LOST_CLI_OPTION
            help
        };

        static struct option long_options[] = {
#define LOST_CLI_OPTION(name, type, prop, defaultVal, converter, defaultArg) \
            {name,                                                      \
             defaultArg == 0 ? required_argument : optional_argument, \
             0,                                                         \
             (int)DatabaseCliOption::prop},
#include "database-options.hpp" // NOLINT
#undef LOST_CLI_OPTION
                {"help", no_argument, 0, (int) DatabaseCliOption::help},
                {0}
        };

        DatabaseOptions databaseOptions;
        int index;
        int option;

        while ((option = getopt_long(argc, argv, "", long_options, &index)) != -1) {
            switch (option) {
#define LOST_CLI_OPTION(name, type, prop, defaultVal, converter, defaultArg) \
                case (int)DatabaseCliOption::prop :                     \
                    if (defaultArg == 0) {     \
                        databaseOptions.prop = converter;       \
                    } else {                                    \
                        if (LOST_OPTIONAL_OPTARG()) {           \
                            databaseOptions.prop = converter;   \
                        } else {                                \
                            databaseOptions.prop = defaultArg;  \
                        }                                       \
                    }                                           \
            break;
#include "database-options.hpp" // NOLINT
#undef LOST_CLI_OPTION
                case (int) DatabaseCliOption::help :std::cout << documentation_database_txt << std::endl;
                    return 0;
                    break;
                default :std::cout << "Illegal flag" << std::endl;
                    exit(1);
            }
        }

        lost::DatabaseBuild(databaseOptions);

    } else if (command == "pipeline") {

        enum class PipelineCliOption {
#define LOST_CLI_OPTION(name, type, prop, defaultVal, converter, defaultArg) prop,
#include "pipeline-options.hpp"
#undef LOST_CLI_OPTION
            help
        };

        static struct option long_options[] = {
#define LOST_CLI_OPTION(name, type, prop, defaultVal, converter, defaultArg) \
            {name,                                                      \
             defaultArg == 0 ? required_argument : optional_argument, \
             0,                                                         \
             (int)PipelineCliOption::prop},
#include "pipeline-options.hpp" // NOLINT
#undef LOST_CLI_OPTION

                // DATABASES
                {"help", no_argument, 0, (int) PipelineCliOption::help},
                {0, 0, 0, 0}
        };

        lost::PipelineOptions pipelineOptions;
        int index;
        int option;

        while ((option = getopt_long(argc, argv, "", long_options, &index)) != -1) {
            switch (option) {
#define LOST_CLI_OPTION(name, type, prop, defaultVal, converter, defaultArg) \
                case (int)PipelineCliOption::prop :                         \
                    if (defaultArg == 0) {    \
                        pipelineOptions.prop = converter;       \
                    } else {                                    \
                        if (LOST_OPTIONAL_OPTARG()) {           \
                            pipelineOptions.prop = converter;   \
                        } else {                                \
                            pipelineOptions.prop = defaultArg;  \
                        }                                       \
                    }                                           \
            break;
#include "pipeline-options.hpp" // NOLINT
#undef LOST_CLI_OPTION
                case (int) PipelineCliOption::help :std::cout << documentation_pipeline_txt << std::endl;
                    return 0;
                    break;
                default :std::cout << "Illegal flag" << std::endl;
                    exit(1);
            }
        }

        lost::PipelineRun(pipelineOptions);

    } else if (command == "sfml") {
        std::cout << "SFML command invoked" << "\n";

        enum class PipelineCliOption {
            #define LOST_CLI_OPTION(name, type, prop, defaultVal, converter, defaultArg) prop,
            #include "pipeline-options.hpp"
            #undef LOST_CLI_OPTION
                        help
        };


        static struct option long_options[] = {
            #define LOST_CLI_OPTION(name, type, prop, defaultVal, converter, defaultArg) \
                        {name,                                                      \
                        defaultArg == 0 ? required_argument : optional_argument, \
                        0,                                                         \
                        (int)PipelineCliOption::prop},
            #include "pipeline-options.hpp" // NOLINT
            #undef LOST_CLI_OPTION

                            // DATABASES
                            {"help", no_argument, 0, (int) PipelineCliOption::help},
                            {0, 0, 0, 0}
        };


lost::PipelineOptions pipelineOptions;
        int index;
        int option;

        while ((option = getopt_long(argc, argv, "", long_options, &index)) != -1) {
            switch (option) {
                #define LOST_CLI_OPTION(name, type, prop, defaultVal, converter, defaultArg) \
                case (int)PipelineCliOption::prop :                         \
                    if (defaultArg == 0) {    \
                        pipelineOptions.prop = converter;       \
                        } else {                                    \
                            if (LOST_OPTIONAL_OPTARG()) {           \
                                pipelineOptions.prop = converter;   \
                            } else {                                \
                                pipelineOptions.prop = defaultArg;  \
                            }                                       \
                        }                                           \
                break;


                #include "pipeline-options.hpp" // NOLINT
                #undef LOST_CLI_OPTION
                case (int) PipelineCliOption::help :std::cout << documentation_pipeline_txt << std::endl;
                        return 0;
                        break;
                    default :std::cout << "Illegal flag" << std::endl;
                        exit(1);
            }

            // print option
            std::cout << option << " b " << "\n";
        }



        std::vector<dost_ImgData> imgData = lost::PipelineRunSFML(pipelineOptions);

        pipelineOptions.regenFalseDb = false; // disable regen for WASDQE panning.
        

        // --------------------------------------
        // SFML Setup
        // --------------------------------------
        // Initiate window and frame image holders.

        // check the resolution of a given photo. can we resize upon frame change? sfml limit testing!

        int maxResY = sf::VideoMode::getDesktopMode().height - 200;

        for (int i = 0; i < (int)imgData.size(); i++) {
            // FIX 1: Float cast to preserve aspect ratio
            float aspect = static_cast<float>(imgData[i].resx) / static_cast<float>(imgData[i].resy);
            
            int imgMaxResY = std::min(imgData[i].resy, maxResY);
            imgData[i].realResy = imgMaxResY;
            imgData[i].realResx = static_cast<int>(imgMaxResY * aspect);
        }

        sf::RenderWindow window(sf::VideoMode(imgData[0].realResx, imgData[0].realResy), "LOST Animation");

        

        bool showStarBoxes = true;

        std::vector<std::string> filePaths;

        if (!pipelineOptions.imageDir.empty()) {

            DIR *dir;
            struct dirent *ent;
            if ((dir = opendir(pipelineOptions.imageDir.c_str())) != NULL) {
                while ((ent = readdir(dir)) != NULL) {
                    std::string filename = ent->d_name;

                    if (filename == "." || filename == "..") continue;

                    if (filename.length() >= 4 && filename.substr(filename.length() - 4) == ".png") {
                        filePaths.push_back(pipelineOptions.imageDir + "/" + filename);
                    }
                }
                closedir(dir);
                
                // Sort the file paths so they match the pipeline data!
                std::sort(filePaths.begin(), filePaths.end());
            }

        } else {

            for (int frame = 0; frame < pipelineOptions.frames; frame++) {
                char buffer[256];
                snprintf(buffer, sizeof(buffer), "sfml-tests/frame_%04d.png", frame);
                filePaths.push_back(std::string(buffer));
            }
        }

        // --------------------------------------
        // JIT Texture Loading Setup
        // --------------------------------------
        sf::Texture currentTexture;
        sf::Sprite currentSprite;
        std::vector<float> scaleFactors(filePaths.size(), 1.0f);

        auto LoadFrame = [&](int idx) {
            if (idx < 0 || idx >= filePaths.size()) return;
            
            TextureInfo info = LoadTextureSafe(filePaths[idx]);
            currentTexture = std::move(info.texture); 
            currentSprite.setTexture(currentTexture, true); 
            
            if (idx >= scaleFactors.size()) {
                scaleFactors.resize(idx + 1, 1.0f);
            }
            scaleFactors[idx] = info.scaleFactor;
        };

        int image_idx = 0;
        if (!filePaths.empty()) {
            LoadFrame(image_idx);
        }
            
        sf::Font font;
        if (!font.loadFromFile("arial.ttf")) { 
            std::cerr << "Failed to load font (place arial.ttf or other .ttf in the working directory)\n";
            return 1;
        }

        sf::Text text;
        text.setFont(font);
        text.setString("Attitude is UNKNOWN");


        auto UpdateHUD = [&](int idx) {
                if (imgData[idx].attitude.IsKnown()) {
                    EulerAngles s = imgData[idx].attitude.ToSpherical();
                    text.setString(
                        "RA: " + std::to_string(RadToDeg(s.ra)) +
                        " DE: " + std::to_string(RadToDeg(s.de)) +
                        " Roll: " + std::to_string(RadToDeg(s.roll))
                    );
                } else {
                    text.setString("Attitude is UNKNOWN");
                }
            };


        UpdateHUD(image_idx);

        std::vector<int> starToCatalogIndex;

        sfml::UpdateStarCatalogMapping(imgData[image_idx], starToCatalogIndex);



        auto starsNames = sfml::loadStarNames("starnames.csv");

        text.setCharacterSize(24);        
        text.setFillColor(sf::Color::Green); 

        const float margin = 6.f;
        text.setPosition(margin, margin);


        sf::Clock scrubTimer; // Timer to control frame scrubbing speed

        bool isExporting = false;
        int export_idx = 0;



        // --------------------------------------
        // Main loop
        // --------------------------------------
        while (window.isOpen())
        {

            sf::Event event;
            while (window.pollEvent(event))
            {
                if (event.type == sf::Event::Closed)
                    window.close();


                if (event.type == sf::Event::KeyPressed)
                {

                    // Only allow a frame change if 25 milliseconds have passed since the last one (~40 FPS)
                    if (scrubTimer.getElapsedTime().asMilliseconds() > 8) { 
                        
                        if (event.key.code == sf::Keyboard::Right) {
                            image_idx = (image_idx + 1) % filePaths.size();
                            LoadFrame(image_idx);
                            UpdateHUD(image_idx);
                            
                            window.setSize(sf::Vector2u(imgData[image_idx].realResx, imgData[image_idx].realResy)); // resize window to fit new image
                            starToCatalogIndex.assign(imgData[image_idx].stars.size(), -1);
                            sfml::UpdateStarCatalogMapping(imgData[image_idx], starToCatalogIndex);
                            
                            scrubTimer.restart(); // Reset the timer after a successful load
                        }
                        
                        if (event.key.code == sf::Keyboard::Left) {
                            image_idx = (image_idx - 1 + filePaths.size()) % filePaths.size();
                            LoadFrame(image_idx);
                            UpdateHUD(image_idx);

                            window.setSize(sf::Vector2u(imgData[image_idx].realResx, imgData[image_idx].realResy)); // resize window to fit new imagere

                            starToCatalogIndex.assign(imgData[image_idx].stars.size(), -1); 

                            sfml::UpdateStarCatalogMapping(imgData[image_idx], starToCatalogIndex);
                            
                            scrubTimer.restart(); // Reset the timer after a successful load
                        }
                    }



                    // messy

                    if (event.key.code == sf::Keyboard::A || event.key.code == sf::Keyboard::D ||
                        event.key.code == sf::Keyboard::W || event.key.code == sf::Keyboard::S ||
                        event.key.code == sf::Keyboard::Q || event.key.code == sf::Keyboard::E) {

                        if (image_idx < (int)filePaths.size() - 1) {
                            int newSize = image_idx + 1;
                            
                            filePaths.resize(newSize); // Resize path vector instead of textures
                            imgData.resize(newSize);
                            
                            pipelineOptions.frames = newSize;

                            pipelineOptions.raMax = imgData[image_idx].trueRa;
                            pipelineOptions.decMax = imgData[image_idx].trueDec;
                            pipelineOptions.rollMax = imgData[image_idx].trueRoll;
                        }

                        // Adjust max attitude based on keypresses, since we are modifying the last frame we only need to adjust max
                        pipelineOptions.raMax -= 2.0f*(event.key.code == sf::Keyboard::A ? -1.0f : 0.0f) + 2.0f*(event.key.code == sf::Keyboard::D ? 1.0f : 0.0f);
                        if (pipelineOptions.raMax > 360.0f) pipelineOptions.raMax -= 360.0f;
                        if (pipelineOptions.raMax < 0.0f) pipelineOptions.raMax += 360.0f;
                        pipelineOptions.decMax += 2.0f*(event.key.code == sf::Keyboard::W ? 1.0f : 0.0f) + 2.0f*(event.key.code == sf::Keyboard::S ? -1.0f : 0.0f);
                        if (pipelineOptions.decMax > 90.0f) pipelineOptions.decMax = 90.0f;
                        if (pipelineOptions.decMax < -90.0f) pipelineOptions.decMax = -90.0f;
                        pipelineOptions.rollMax += 5.0f*(event.key.code == sf::Keyboard::Q ? -1.0f : 0.0f) + 5.0f*(event.key.code == sf::Keyboard::E ? 1.0f : 0.0f);
                        if (pipelineOptions.rollMax > 360.0f) pipelineOptions.rollMax -= 360.0f;
                        if (pipelineOptions.rollMax < 0.0f) pipelineOptions.rollMax += 360.0f;

                        std::cout << "True Attitude: RA " << imgData[image_idx].trueRa << " DE " << imgData[image_idx].trueDec << " Roll " << imgData[image_idx].trueRoll << "\n";
                        if (pipelineOptions.trackingMode == true) {
                            if (imgData[image_idx].attitude.IsKnown()) {
                                EulerAngles eul = imgData[image_idx].attitude.ToSpherical();

                                // FIX 1: Set the base position to the tracker's OWN last estimate, not the ground truth.
                                // This stops the permanent visual offset.
                                pipelineOptions.lastRa = eul.ra; 
                                pipelineOptions.lastDec = eul.de;
                                pipelineOptions.lastRoll = eul.roll;

                                // Helper lambda to find the shortest angular distance between two radians
                                auto shortestAngle = [](float target, float current) {
                                    float diff = target - current;
                                    while (diff > M_PI) diff -= 2.0f * M_PI;
                                    while (diff < -M_PI) diff += 2.0f * M_PI;
                                    return diff;
                                };

                                // FIX 2: Calculate the shortest path velocity so it doesn't spin 358 degrees
                                pipelineOptions.raVelocity = shortestAngle((pipelineOptions.raMax) * M_PI/180.0f, eul.ra);
                                pipelineOptions.decVelocity = shortestAngle((pipelineOptions.decMax) * M_PI/180.0f, eul.de);
                                pipelineOptions.rollVelocity = shortestAngle((pipelineOptions.rollMax) * M_PI/180.0f, eul.roll); 

                                pipelineOptions.timeBetweenFrame = 1.0; 
                            }
                            else {
                                pipelineOptions.timeBetweenFrame = -1.0; 
                            }
                        }

                        pipelineOptions.panning = true;
                        pipelineOptions.frames += 1;


                        std::vector<dost_ImgData> imgDataTemp = lost::PipelineRunSFML(pipelineOptions);

                        imgData.push_back(imgDataTemp[0]);

                        starToCatalogIndex.resize(imgData.back().stars.size(), -1); 

                        sfml::UpdateStarCatalogMapping(imgData[image_idx], starToCatalogIndex);

                        char buffer[256];

                        snprintf(buffer, sizeof(buffer), "sfml-tests/frame_%04zu.png", filePaths.size()); // Use filePaths length

                        // Track the new file path instead of loading directly to deque
                        filePaths.push_back(std::string(buffer));
                        
                        image_idx = filePaths.size() - 1;     // jump forward to new image
                        
                        LoadFrame(image_idx); // Load dynamically

                        UpdateHUD(image_idx);

                        sfml::UpdateStarCatalogMapping(imgData[image_idx], starToCatalogIndex);
                    }
                    else if (event.key.code == sf::Keyboard::J) {
                        showStarBoxes = !showStarBoxes;

                    } else if (event.key.code == sf::Keyboard::V && !isExporting) {
                        std::cout << "\nStarting video export...\n";
                        system("mkdir -p video_export"); // Create temp folder
                        isExporting = true;
                        export_idx = 0;
                    }

                }

            }

            if (isExporting) {
                image_idx = export_idx;
                LoadFrame(image_idx);
                UpdateHUD(image_idx);
                window.setSize(sf::Vector2u(imgData[image_idx].realResx, imgData[image_idx].realResy));
                starToCatalogIndex.assign(imgData[image_idx].stars.size(), -1);
                sfml::UpdateStarCatalogMapping(imgData[image_idx], starToCatalogIndex);
            }

            //display text in the top left of current attitude

            window.clear();


            sf::View worldView(sf::FloatRect(0.f, 0.f, (float)imgData[image_idx].resx, (float)imgData[image_idx].resy));
            window.setView(worldView);

            window.draw(currentSprite); // Draw the single actively loaded sprite

            float s = scaleFactors[image_idx];

            auto& stars = imgData[image_idx].stars;
            auto& starIds = imgData[image_idx].starIds;

            sf::Vector2f sum(0.f, 0.f);
            int count = 0;

            for (std::pair<int,int> id : starIds) {
                if (id.first >= 0 && id.first < (int)stars.size()) {
                    sum.x += stars[id.first].position.x;
                    sum.y += stars[id.first].position.y;
                    count++;
                }
            }

            sf::Vector2f center;

            if (count > 0) { // A center exists
                center = sf::Vector2f(sum.x / count, sum.y / count);
            }


            // i wanna see if there is a more efficient way to do this

            for (size_t i = 0; i < stars.size(); i++) {
                Star& star = stars[i];

                // pair with .first as starIndex, .second as catalogIndex we care about indexing with first
                //bool isMatched = (std::find(starIds.begin(), starIds.end(), std::make_pair(i, 0)) != starIds.end());
                

                int pairindex = starToCatalogIndex[i];

                bool isMatched = (pairindex != -1);

                // Draw box
                if (showStarBoxes || isMatched) {
                    sf::RectangleShape box = sfml::CreateStarBox(star, pairindex != -1);
                    window.draw(box);
                }


                if (isMatched && count > 0) { // A center exists
                    sf::Vertex line[] = {
                        sf::Vertex(center, sf::Color::Cyan),
                        sf::Vertex(sf::Vector2f(star.position.x, star.position.y), sf::Color::Cyan)};

                    // Draw star label
                    
                    sf::Text starText = sfml::CreateStarLabel(star, pairindex, starsNames, font);
                    
                    window.draw(starText);
                    window.draw(line, 2, sf::Lines);
                }
            }

            for (size_t i = 0; i < imgData[image_idx].trackedStars.size(); i++) {
                // dynamicWindowSize is the full width, so we divide by 2 for the radius
                float radius = imgData[image_idx].trackedStars[i].size / 2.0f; 

                sf::CircleShape circle(radius);
                circle.setFillColor(sf::Color::Transparent);
                circle.setOutlineColor(sf::Color::Red);
                circle.setOutlineThickness(1.2f);
                
                // Set the origin to the exact mathematical center of the circle
                circle.setOrigin(radius, radius); 
                
                circle.setPosition(imgData[image_idx].trackedStars[i].projectedX, imgData[image_idx].trackedStars[i].projectedY);
                window.draw(circle);
            }

            // if trueRa, trueDec exist, draw true attitude as text in top left
            if (imgData[image_idx].trueRa != 0 || imgData[image_idx].trueDec != 0 || imgData[image_idx].trueRoll != 0) {
                sf::Text trueAttitudeText;
                trueAttitudeText.setFont(font);
                trueAttitudeText.setCharacterSize(18);
                trueAttitudeText.setFillColor(sf::Color::White);
                trueAttitudeText.setPosition(margin, margin + 30.f); // below the estimated attitude

                trueAttitudeText.setString(
                    "True RA: " + std::to_string(imgData[image_idx].trueRa) +
                    " True DE: " + std::to_string(imgData[image_idx].trueDec) +
                    " True Roll: " + std::to_string(imgData[image_idx].trueRoll)
                );

                window.draw(trueAttitudeText);
            }

            window.setView(window.getDefaultView());
            window.draw(text);


            float hudY = window.getSize().y - 80.0f;
            sf::Vector2f estPos(60.0f, hudY);
            sf::Vector2f truePos(160.0f, hudY);

            // 1. Draw Estimated Gimbal
            if (imgData[image_idx].attitude.IsKnown()) {
                EulerAngles s = imgData[image_idx].attitude.ToSpherical();
                DrawGimbal(window, RadToDeg(s.ra), RadToDeg(s.de), RadToDeg(s.roll), 
                        estPos, "ESTIMATED", font, sf::Color::Cyan);
            }

            // 2. Draw True Gimbal
            DrawGimbal(window, imgData[image_idx].trueRa, imgData[image_idx].trueDec, imgData[image_idx].trueRoll, 
                    truePos, "GROUND TRUTH", font, sf::Color::White);

            window.display();

            if (isExporting) {
                sf::Texture captureTexture;
                captureTexture.create(window.getSize().x, window.getSize().y);
                captureTexture.update(window);
                
                char outBuf[256];
                snprintf(outBuf, sizeof(outBuf), "video_export/frame_%04d.png", export_idx);
                captureTexture.copyToImage().saveToFile(outBuf);
                
                std::cout << "Exported frame " << export_idx + 1 << "/" << filePaths.size() << "\r" << std::flush;
                
                export_idx++;
                if (export_idx >= (int)filePaths.size()) {
                    isExporting = false;
                    std::cout << "\nAll frames saved. Encoding MP4 with FFmpeg...\n";
                    
                    // Call FFmpeg to stitch the images. 
                    // -y overwrites existing file, -framerate 20 sets FPS
                    system("ffmpeg -y -framerate 20 -i video_export/frame_%04d.png -c:v libx264 -crf 28 -preset faster -pix_fmt yuv420p lost_demo.mp4");
                    
                    std::cout << "Video saved as lost_demo.mp4 in your build directory!\n";
                }
            }

            

            sf::sleep(sf::milliseconds(12));
        }
    } else {
        std::cout << "Usage: ./lost database or ./lost pipeline" << std::endl
                  << "Use --help flag on those commands for further help" << std::endl;
    }
    return 0;
}

}

int main(int argc, char **argv) {
    return lost::LostMain(argc, argv);
}