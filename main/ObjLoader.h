#pragma once

#include "Object.hpp"
#include "Material.hpp"
#include <cstdio>
#include <cstring>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"

namespace Loader
{
// Scoped to namespace Loader so the class `Renderer` (a member of namespace
// `Renderer`) doesn't leak into the global namespace, which on MSVC would
// make `Renderer::Object` ambiguous (C2872) in any TU including this header.
using namespace Renderer;
    [[maybe_unused]] static Texture* LoadRawBMP(const char *fileName) {
        FILE *file = fopen(fileName, "rb");
        if (!file) {
            printf("Error: Unable to open file %s\n", fileName);
            return nullptr;
        }

        // Read header
        uint8_t header[54];
        if (fread(header, 1, 54, file) != 54) {
            printf("Error: Failed to read BMP header\n");
            fclose(file);
            return nullptr;
        }

        // Basic checks
        if (header[0] != 'B' || header[1] != 'M') {
            printf("Error: Not a BMP file\n");
            fclose(file);
            return nullptr;
        }

        int dataOffset = *(int*)&header[10];
        int width = *(int*)&header[18];
        int height = *(int*)&header[22];
        int bitsPerPixel = *(short*)&header[28];

        if (bitsPerPixel != 16) {
            printf("Error: Only 16-bit BMP images are supported (got %d)\n", bitsPerPixel);
            fclose(file);
            return nullptr;
        }

        // Seek to pixel data and read
        fseek(file, dataOffset, SEEK_SET);
        size_t pixelCount = (size_t)width * (size_t)abs(height);
        uint16_t *data = (uint16_t*)malloc(pixelCount * sizeof(uint16_t));
        if (!data) {
            printf("Error: Out of memory allocating BMP pixel buffer\n");
            fclose(file);
            return nullptr;
        }

        size_t read = fread(data, sizeof(uint16_t), pixelCount, file);
        if (read != pixelCount) {
            printf("Warning: BMP read size mismatch (read %zu expected %zu)\n", read, pixelCount);
        }

        // Construct texture (caller owns the data via the Texture)
        Texture *texture = new Texture(width, abs(height), data, false, 0, false, TextureAddressMode::WRAP);

        fclose(file);
        return texture;
    }

    static void LoadMtlData(const char *fileData, std::vector<Material*> *materialLibrary, std::vector<Texture*> *textureLibrary) {
        const char *line = fileData;
        Material *currentMaterial = nullptr;

        while (*line)
        {
            if (line[0] == 'n' && line[1] == 'e')
            {
                char materialName[256];
                sscanf(line, "newmtl %s", materialName);
                currentMaterial = new Material();
                currentMaterial->name = strdup(materialName);  // Use strdup to allocate memory for the name
                materialLibrary->push_back(currentMaterial);
                printf("Loaded material %s\n", currentMaterial->name);
            }
            else if (currentMaterial != nullptr) // Ensure currentMaterial is not null before assigning properties
            {
                if (line[0] == 'K' && line[1] == 'd')
                {
                    float r, g, b;
                    sscanf(line, "Kd %f %f %f", &r, &g, &b);
                    int r8 = (int)(r * 255.0f + 0.5f);
                    int g8 = (int)(g * 255.0f + 0.5f);
                    int b8 = (int)(b * 255.0f + 0.5f);
                    if (r8 < 0) r8 = 0;
                    if (r8 > 255) r8 = 255;
                    if (g8 < 0) g8 = 0;
                    if (g8 > 255) g8 = 255;
                    if (b8 < 0) b8 = 0;
                    if (b8 > 255) b8 = 255;
                    int r5 = (r8 * 31 + 127) / 255;
                    int g6 = (g8 * 63 + 127) / 255;
                    int b5 = (b8 * 31 + 127) / 255;
                    currentMaterial->color = (uint16_t)((r5 << 11) | (g6 << 5) | b5);
                }
                else if (line[0] == 'K' && line[1] == 'e')
                {
                    // Emissive colour (self-lit). Only mark emissive when Ke is non-zero.
                    float r, g, b;
                    sscanf(line, "Ke %f %f %f", &r, &g, &b);
                    const float EMISSIVE_EPS = 1e-3f;
                    if (r > EMISSIVE_EPS || g > EMISSIVE_EPS || b > EMISSIVE_EPS) {
                        int r8 = (int)(r * 255.0f + 0.5f);
                        int g8 = (int)(g * 255.0f + 0.5f);
                        int b8 = (int)(b * 255.0f + 0.5f);
                        if (r8 < 0) r8 = 0;
                        if (r8 > 255) r8 = 255;
                        if (g8 < 0) g8 = 0;
                        if (g8 > 255) g8 = 255;
                        if (b8 < 0) b8 = 0;
                        if (b8 > 255) b8 = 255;
                        int r5 = (r8 * 31 + 127) / 255;
                        int g6 = (g8 * 63 + 127) / 255;
                        int b5 = (b8 * 31 + 127) / 255;
                        currentMaterial->color = (uint16_t)((r5 << 11) | (g6 << 5) | b5);
                        currentMaterial->emissive = true;
                    }
                }
                else if (line[0] == 'd')
                {
                    float alpha;
                    sscanf(line, "d %f", &alpha);
                    currentMaterial->alpha = static_cast<uint8_t>(alpha * 255);
                }
                else if (line[0] == 'm' && line[1] == 'a')
                {
                    if (textureLibrary == nullptr)
                    {
                        printf("Warning: Texture library not provided, ignoring texture\n");
                        continue;
                    }
                    char textureName[256];
                    sscanf(line, "map_Kd %s", textureName);
                    // Find a texture with the given name in the library - if none exists, create it and add it
                    bool found = false;
                    for (auto texture : *textureLibrary)
                    {
                        if (strcmp(texture->name, textureName) == 0)
                        {
                            currentMaterial->diffuseMap = texture;
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                    {
                        Texture *newTexture = new Texture(0, 0, nullptr, false, 0, false, TextureAddressMode::WRAP);
                        textureLibrary->push_back(newTexture);
                        currentMaterial->diffuseMap = newTexture;
                    }
                }
            }

            // Move to the next line
            while (*line && *line != '\n')
                line++;
            if (*line)
                line++;
        }
    }

    // `scale` is a runtime multiplier applied to every loaded vertex
    // position on top of the engine's internal FIXED_POINT_SCALE/8
    // baseline. Use it to enlarge content authored at "unit = 1 world
    // unit" so the renderer runs at a coarser world grid — useful at
    // high screen resolutions where each world unit is several pixels
    // and the renderer's integer rotation chain produces visible vertex
    // wobble. UVs and normals are not affected. Default 1.0f preserves
    // the historical behaviour.
    static Object *LoadFromObjData(const char *fileData, Material *defaultMaterial = nullptr, std::vector<Material*> *materialLibrary = nullptr, float scale = 1.0f)
    {
        Object *obj = new Object();
        const char *line = fileData;
        std::vector<float> tempVertices;
        std::vector<float> tempUVs;
        std::vector<float> tempNormals;
        Material *currentMaterial = defaultMaterial;

        const float SCALE = (FIXED_POINT_SCALE / 8) * scale;

        while (*line)
        {
            //Custom: "mr" for material reset - revert to default material
            if (line[0] == 'm' && line[1] == 'd')
            {
                currentMaterial = defaultMaterial;
            }
            //Custom: "mt <16-bit hex> <optional 0-255 alpha, default 255>" for material set
            else if (line[0] == 'm' && line[1] == 't')
            {
                uint16_t color;
                uint8_t alpha = 255;
                sscanf(line, "mt %hx %hhu", &color, &alpha);
                currentMaterial = new Material(color, alpha);
            }
            // "usemtl" handler
            else if (line[0] == 'u' && line[1] == 's')
            {
                char materialName[256];
                sscanf(line, "usemtl %s", materialName);
                if (materialLibrary == nullptr)
                {
                    printf("Warning: Material library not provided, ignoring material\n");
                }
                else 
                {
                    bool materialFound = false;
                    for (auto material : *materialLibrary)
                    {
                        if (strcmp(material->name, materialName) == 0)
                        {
                            currentMaterial = material;
                            materialFound = true;
                            break;
                        }
                    }
                    if (!materialFound)
                    {
                        printf("Warning: Material '%s' not found in the library\n", materialName);
                        printf("Available materials are:\n");
                        for (auto material : *materialLibrary)
                        {
                            printf("  %s\n", material->name);
                        }
                    }
                }
            }
            else if (line[0] == 'v' && line[1] == ' ')
            {
                float x, y, z;
                sscanf(line, "v %f %f %f", &x, &y, &z);
                tempVertices.push_back(x);
                tempVertices.push_back(y);
                tempVertices.push_back(z);
            }
            else if (line[0] == 'v' && line[1] == 't')
            {
                float u, v;
                sscanf(line, "vt %f %f", &u, &v);
                tempUVs.push_back(u);
                tempUVs.push_back(v);
            }
            else if (line[0] == 'v' && line[1] == 'n')
            {
                float x, y, z;
                sscanf(line, "vn %f %f %f", &x, &y, &z);
                tempNormals.push_back(x);
                tempNormals.push_back(y);
                tempNormals.push_back(z);
            }
            else if (line[0] == 'f')
            {
                int vertices[4][3] = {0}; // Array to store up to 4 vertices (v/t/n format)
                int numVertices = 0;
                const char* ptr = line + 1;
                
                // Skip whitespace
                while (*ptr == ' ') ptr++;
                
                // Parse up to 4 vertices
                for (int i = 0; i < 4 && *ptr && *ptr != '\n'; i++) {
                    if (sscanf(ptr, "%d/%d/%d", &vertices[i][0], &vertices[i][1], &vertices[i][2]) == 3) {
                        numVertices++;
                        // Skip to next vertex
                        while (*ptr && *ptr != ' ' && *ptr != '\n') ptr++;
                        while (*ptr == ' ') ptr++;
                    }
                }

                // Function to create and add a vertex
                auto createVertex = [&](int v, int t, int n) -> Object::Vertex {
                    Object::Vertex vert;
                    vert.position = {
                        (int32_t)(tempVertices[(v - 1) * 3] * SCALE),
                        (int32_t)(tempVertices[(v - 1) * 3 + 1] * SCALE),
                        (int32_t)(tempVertices[(v - 1) * 3 + 2] * SCALE)
                    };
                    vert.uv = {
                        (int32_t)(tempUVs[(t - 1) * 2] * FIXED_POINT_SCALE),
                        (int32_t)(tempUVs[(t - 1) * 2 + 1] * FIXED_POINT_SCALE)
                    };
                    // Populate normal from parsed vn entries (scale to fixed-point)
                    if (n > 0 && (size_t)((n - 1) * 3 + 2) < tempNormals.size()) {
                        vert.normal.x = (int32_t)(tempNormals[(n - 1) * 3] * FIXED_POINT_SCALE);
                        vert.normal.y = (int32_t)(tempNormals[(n - 1) * 3 + 1] * FIXED_POINT_SCALE);
                        vert.normal.z = (int32_t)(tempNormals[(n - 1) * 3 + 2] * FIXED_POINT_SCALE);
                    }
                    return vert;
                };

                // Handle triangle
                if (numVertices == 3) {
                    Object::Vertex vert1 = createVertex(vertices[0][0], vertices[0][1], vertices[0][2]);
                    Object::Vertex vert2 = createVertex(vertices[1][0], vertices[1][1], vertices[1][2]);
                    Object::Vertex vert3 = createVertex(vertices[2][0], vertices[2][1], vertices[2][2]);

                    obj->addVertex(vert1);
                    obj->addVertex(vert2);
                    obj->addVertex(vert3);
                    obj->addTriangle(obj->vertices.size() - 3, obj->vertices.size() - 2, obj->vertices.size() - 1, currentMaterial);
                }
                // Handle quad by splitting into two triangles
                else if (numVertices == 4) {
                    Object::Vertex vert1 = createVertex(vertices[0][0], vertices[0][1], vertices[0][2]);
                    Object::Vertex vert2 = createVertex(vertices[1][0], vertices[1][1], vertices[1][2]);
                    Object::Vertex vert3 = createVertex(vertices[2][0], vertices[2][1], vertices[2][2]);
                    Object::Vertex vert4 = createVertex(vertices[3][0], vertices[3][1], vertices[3][2]);

                    // First triangle (0,1,2)
                    obj->addVertex(vert1);
                    obj->addVertex(vert2);
                    obj->addVertex(vert3);
                    obj->addTriangle(obj->vertices.size() - 3, obj->vertices.size() - 2, obj->vertices.size() - 1, currentMaterial);

                    // Second triangle (0,2,3)
                    obj->addVertex(vert1);
                    obj->addVertex(vert3);
                    obj->addVertex(vert4);
                    obj->addTriangle(obj->vertices.size() - 3, obj->vertices.size() - 2, obj->vertices.size() - 1, currentMaterial);
                }
            }

            // Advance to next line
            while (*line && *line != '\n')
                line++;
            if (*line)
                line++;
        }

        obj->calculateBoundingBox();

        return obj;
    }

    [[maybe_unused]] static Object *LoadFromObjFile(char *filename, Material *defaultMaterial = nullptr, float scale = 1.0f) {
        FILE *file = fopen(filename, "rb");
        if (!file) {
            return nullptr;
        }

        fseek(file, 0, SEEK_END);
        long fileSize = ftell(file);
        fseek(file, 0, SEEK_SET);

        char *fileData = new char[fileSize + 1];
        fread(fileData, 1, fileSize, file);
        fileData[fileSize] = 0;

        fclose(file);

        Object *obj = LoadFromObjData(fileData, defaultMaterial, nullptr, scale);
        delete[] fileData;

        return obj;
    }
}

#pragma GCC diagnostic pop