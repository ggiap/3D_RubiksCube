// Compile with:
// g++ main.cpp -o rubiks -lSDL2 -lGL -lGLEW
// Needs glm/, stb_image.h, and textures/ folder.

#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <vector>
#include <array>
#include <string>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <deque>
#include <ctime>
#include <cstdlib>

const int WINDOW_WIDTH = 1024;
const int WINDOW_HEIGHT = 768;
const int MIN_DRAG_DISTANCE = 20;

// Colors
const glm::vec4 WHITE  = {1.0f, 1.0f, 1.0f, 1.0f};
const glm::vec4 YELLOW = {1.0f, 0.95f, 0.0f, 1.0f};
const glm::vec4 RED    = {1.0f, 0.0f, 0.0f, 1.0f};
const glm::vec4 ORANGE = {1.0f, 0.6f, 0.0f, 1.0f};
const glm::vec4 GREEN  = {0.0f, 1.0f, 0.0f, 1.0f};
const glm::vec4 BLUE   = {0.0f, 0.2f, 1.0f, 1.0f};
const glm::vec4 BLACK_PLASTIC = {0.05f, 0.05f, 0.05f, 1.0f};

enum FaceDir { POS_X=0, NEG_X, POS_Y, NEG_Y, POS_Z, NEG_Z };

// --- Shaders ---
const char* skyboxVS = R"(
    #version 330 core
    layout (location = 0) in vec3 aPos;
    out vec3 TexCoords;
    uniform mat4 projection;
    uniform mat4 view;
    void main() {
        TexCoords = aPos;
        vec4 pos = projection * mat4(mat3(view)) * vec4(aPos, 1.0);
        gl_Position = pos.xyww;
    } 
)";
const char* skyboxFS = R"(
    #version 330 core
    out vec4 FragColor;
    in vec3 TexCoords;
    uniform samplerCube skybox;
    void main() {    
        vec3 envColor = texture(skybox, TexCoords).rgb;
        envColor = envColor * 0.3; 
        envColor = pow(envColor, vec3(1.0/2.2)); 
        FragColor = vec4(envColor, 1.0);
    }
)";
const char* cubeVS = R"(
    #version 330 core
    layout (location = 0) in vec3 aPos;
    layout (location = 1) in vec3 aNormal;
    layout (location = 2) in vec2 aTexCoord;
    uniform mat4 model;
    uniform mat4 view;
    uniform mat4 projection;
    out vec3 WorldPos;
    out vec3 Normal;
    out vec2 TexCoord;
    void main() {
        TexCoord = aTexCoord;
        WorldPos = vec3(model * vec4(aPos, 1.0));
        Normal = mat3(transpose(inverse(model))) * aNormal;   
        gl_Position = projection * view * vec4(WorldPos, 1.0);
    }
)";
const char* cubeFS = R"(
    #version 330 core
    out vec4 FragColor;
    in vec3 WorldPos;
    in vec3 Normal;
    in vec2 TexCoord;
    uniform vec4 uAlbedoColor;
    uniform sampler2D uLogoTexture;
    uniform bool uUseLogo;
    uniform float uRoughness; 
    uniform vec3 uCamPos;
    uniform samplerCube uSkybox;
    float fresnelSchlick(float cosTheta, float F0) { return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0); }
    void main() {
        vec3 N = normalize(Normal);
        vec3 V = normalize(uCamPos - WorldPos);
        vec3 R = reflect(-V, N);
        vec3 albedo = uAlbedoColor.rgb;
        if (uUseLogo) {
            vec4 logo = texture(uLogoTexture, TexCoord);
            albedo = mix(albedo, logo.rgb * albedo, logo.a);
        }
        float F0 = 0.04; 
        float F = fresnelSchlick(max(dot(N, V), 0.0), F0);
        float MAX_REFLECTION_LOD = 4.0; 
        vec3 prefilteredColor = textureLod(uSkybox, R, uRoughness * MAX_REFLECTION_LOD).rgb; 
        vec3 specular = prefilteredColor * F * 1.5; 
        vec3 irradiance = textureLod(uSkybox, N, MAX_REFLECTION_LOD).rgb;
        vec3 diffuse = irradiance * albedo * 1.2; 
        vec3 color = diffuse + specular;
        float exposure = 1.0;
        color = vec3(1.0) - exp(-color * exposure);
        color = pow(color, vec3(1.0/2.2));   
        FragColor = vec4(color, 1.0);
    }
)";

class Shader {
public:
    GLuint ID;
    Shader(const char* vCode, const char* fCode) {
        GLuint vertex = CompileShader(vCode, GL_VERTEX_SHADER);
        GLuint fragment = CompileShader(fCode, GL_FRAGMENT_SHADER);
        ID = glCreateProgram();
        glAttachShader(ID, vertex);
        glAttachShader(ID, fragment);
        glLinkProgram(ID);
        glDeleteShader(vertex);
        glDeleteShader(fragment);
    }
    void use() { glUseProgram(ID); }
    void setMat4(const std::string &name, const glm::mat4 &mat) const { glUniformMatrix4fv(glGetUniformLocation(ID, name.c_str()), 1, GL_FALSE, &mat[0][0]); }
    void setVec3(const std::string &name, const glm::vec3 &value) const { glUniform3fv(glGetUniformLocation(ID, name.c_str()), 1, &value[0]); }
    void setVec4(const std::string &name, const glm::vec4 &value) const { glUniform4fv(glGetUniformLocation(ID, name.c_str()), 1, &value[0]); }
    void setBool(const std::string &name, bool value) const { glUniform1i(glGetUniformLocation(ID, name.c_str()), (int)value); }
    void setFloat(const std::string &name, float value) const { glUniform1f(glGetUniformLocation(ID, name.c_str()), value); }
    void setInt(const std::string &name, int value) const { glUniform1i(glGetUniformLocation(ID, name.c_str()), value); }
private:
    GLuint CompileShader(const char* source, GLenum type) {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &source, NULL);
        glCompileShader(s);
        return s;
    }
};

GLuint loadTexture(const char* path) {
    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    int width, height, nrComponents;
    unsigned char *data = stbi_load(path, &width, &height, &nrComponents, 0);
    if (data) {
        GLenum format = (nrComponents == 4) ? GL_RGBA : GL_RGB;
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        stbi_image_free(data);
    } else {
        unsigned char white[] = {255, 255, 255, 255};
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return textureID;
}

GLuint loadCubemap(std::vector<std::string> faces) {
    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);
    int width, height, nrChannels;
    stbi_set_flip_vertically_on_load(false);
    for (unsigned int i = 0; i < faces.size(); i++) {
        unsigned char *data = stbi_load(faces[i].c_str(), &width, &height, &nrChannels, 0);
        if (data) {
            GLenum format = (nrChannels == 4) ? GL_SRGB_ALPHA : GL_SRGB;
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, format, width, height, 0, (nrChannels == 4 ? GL_RGBA : GL_RGB), GL_UNSIGNED_BYTE, data);
            stbi_image_free(data);
        } else {
            unsigned char gray[] = {50, 50, 50}; 
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, gray);
        }
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP); 
    return textureID;
}

struct Vertex { float x, y, z; float nx, ny, nz; float u, v; };
class CubeMesh {
public:
    GLuint VAO, VBO;
    CubeMesh() {
        float s = 0.495f; 
        std::vector<Vertex> vertices;
        auto addFace = [&](glm::vec3 n, glm::vec3 right, glm::vec3 up) {
            glm::vec3 c = n * s;
            Vertex v1 = {c.x - right.x*s - up.x*s, c.y - right.y*s - up.y*s, c.z - right.z*s - up.z*s, n.x, n.y, n.z, 0.0f, 0.0f};
            Vertex v2 = {c.x + right.x*s - up.x*s, c.y + right.y*s - up.y*s, c.z + right.z*s - up.z*s, n.x, n.y, n.z, 1.0f, 0.0f};
            Vertex v3 = {c.x + right.x*s + up.x*s, c.y + right.y*s + up.y*s, c.z + right.z*s + up.z*s, n.x, n.y, n.z, 1.0f, 1.0f};
            Vertex v4 = {c.x - right.x*s + up.x*s, c.y - right.y*s + up.y*s, c.z - right.z*s + up.z*s, n.x, n.y, n.z, 0.0f, 1.0f};
            vertices.push_back(v1); vertices.push_back(v2); vertices.push_back(v3);
            vertices.push_back(v1); vertices.push_back(v3); vertices.push_back(v4);
        };
        addFace({1,0,0}, {0,0,-1}, {0,1,0}); addFace({-1,0,0}, {0,0,1}, {0,1,0});
        addFace({0,1,0}, {1,0,0}, {0,0,-1}); addFace({0,-1,0}, {1,0,0}, {0,0,1});
        addFace({0,0,1}, {1,0,0}, {0,1,0});  addFace({0,0,-1}, {-1,0,0}, {0,1,0});
        glGenVertexArrays(1, &VAO); glGenBuffers(1, &VBO);
        glBindVertexArray(VAO); glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0); glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(3*sizeof(float))); glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(6*sizeof(float))); glEnableVertexAttribArray(2);
    }
    void drawFace(int faceIdx) { glBindVertexArray(VAO); glDrawArrays(GL_TRIANGLES, faceIdx * 6, 6); }
};

class SkyboxMesh {
public:
    GLuint VAO, VBO;
    SkyboxMesh() {
        float skyboxVertices[] = {
            -1.0f,  1.0f, -1.0f, -1.0f, -1.0f, -1.0f,  1.0f, -1.0f, -1.0f,  1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f, -1.0f,  1.0f, -1.0f,
            -1.0f, -1.0f,  1.0f, -1.0f, -1.0f, -1.0f, -1.0f,  1.0f, -1.0f, -1.0f,  1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f, -1.0f,  1.0f,
             1.0f, -1.0f, -1.0f,  1.0f, -1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f, -1.0f,  1.0f, -1.0f, -1.0f,
            -1.0f, -1.0f,  1.0f, -1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f, -1.0f,  1.0f, -1.0f, -1.0f,  1.0f,
            -1.0f,  1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f, -1.0f,
            -1.0f, -1.0f, -1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f, -1.0f,  1.0f, -1.0f, -1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f
        };
        glGenVertexArrays(1, &VAO); glGenBuffers(1, &VBO);
        glBindVertexArray(VAO); glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxVertices), &skyboxVertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    }
    void draw() { glBindVertexArray(VAO); glDrawArrays(GL_TRIANGLES, 0, 36); glBindVertexArray(0); }
};

enum MoveType { MOVE_F, MOVE_F_PRIME, MOVE_B, MOVE_B_PRIME, MOVE_L, MOVE_L_PRIME, MOVE_R, MOVE_R_PRIME, MOVE_U, MOVE_U_PRIME, MOVE_D, MOVE_D_PRIME, MOVE_M, MOVE_M_PRIME, MOVE_E, MOVE_E_PRIME, MOVE_S, MOVE_S_PRIME, MOVE_NONE };

struct Cubie {
    int x, y, z; std::array<glm::vec4, 6> stickers; bool isCenterFace; 
    Cubie(int px, int py, int pz) : x(px), y(py), z(pz) {
        stickers[POS_X] = (px == 1) ? GREEN : BLACK_PLASTIC; stickers[NEG_X] = (px == -1) ? BLUE : BLACK_PLASTIC;
        stickers[POS_Y] = (py == 1) ? WHITE : BLACK_PLASTIC; stickers[NEG_Y] = (py == -1) ? YELLOW : BLACK_PLASTIC;
        stickers[POS_Z] = (pz == 1) ? RED : BLACK_PLASTIC; stickers[NEG_Z] = (pz == -1) ? ORANGE : BLACK_PLASTIC;
        isCenterFace = (x == 0 && y == 0 && z == 1);
    }
    void rotateX(int times) { times = ((times % 4) + 4) % 4; for (int i = 0; i < times; i++) { int newY = -z; int newZ = y; y = newY; z = newZ; auto temp = stickers[POS_Y]; stickers[POS_Y] = stickers[NEG_Z]; stickers[NEG_Z] = stickers[NEG_Y]; stickers[NEG_Y] = stickers[POS_Z]; stickers[POS_Z] = temp; } }
    void rotateY(int times) { times = ((times % 4) + 4) % 4; for (int i = 0; i < times; i++) { int newX = z; int newZ = -x; x = newX; z = newZ; auto temp = stickers[POS_X]; stickers[POS_X] = stickers[POS_Z]; stickers[POS_Z] = stickers[NEG_X]; stickers[NEG_X] = stickers[NEG_Z]; stickers[NEG_Z] = temp; } }
    void rotateZ(int times) { times = ((times % 4) + 4) % 4; for (int i = 0; i < times; i++) { int newX = -y; int newY = x; x = newX; y = newY; auto temp = stickers[POS_X]; stickers[POS_X] = stickers[NEG_Y]; stickers[NEG_Y] = stickers[NEG_X]; stickers[NEG_X] = stickers[POS_Y]; stickers[POS_Y] = temp; } }

    void draw(Shader& shader, CubeMesh& mesh, glm::mat4 modelMatrix, GLuint logoTex, GLuint skyboxTex) {
        shader.setMat4("model", modelMatrix);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_CUBE_MAP, skyboxTex); shader.setInt("uSkybox", 1);
        for (int i = 0; i < 6; i++) {
            bool useLogo = (isCenterFace && i == POS_Z);
            bool isSticker = (stickers[i] != BLACK_PLASTIC);
            shader.setVec4("uAlbedoColor", stickers[i]);
            shader.setFloat("uRoughness", isSticker ? 0.2f : 0.4f); 
            shader.setBool("uUseLogo", useLogo);
            if (useLogo) { glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, logoTex); shader.setInt("uLogoTexture", 0); }
            mesh.drawFace(i);
        }
    }
};

class RubiksCube {
private:
    std::vector<Cubie> cubies; CubeMesh mesh; GLuint logoTexture; GLuint skyboxTexture;
    bool animating; MoveType currentMove; float animationAngle, targetAngle, animationSpeed;
    std::vector<size_t> animatingCubies; int rotAxis; int rotDirection;
    std::vector<std::array<int, 3>> originalPositions;
    float cameraRotX, cameraRotY, cameraDistance; glm::mat4 viewMatrix, projMatrix; glm::vec3 camPos;
    
    // Move Queue for Solving
    std::deque<MoveType> moveQueue;
    std::vector<MoveType> history;
    bool autoSolving;

    void updateMatrices() {
        projMatrix = glm::perspective(glm::radians(40.0f), (float)WINDOW_WIDTH/WINDOW_HEIGHT, 0.1f, 100.0f);
        float radX = glm::radians(cameraRotX), radY = glm::radians(cameraRotY);
        camPos.x = cameraDistance * cos(radX) * sin(radY); camPos.y = cameraDistance * sin(radX); camPos.z = cameraDistance * cos(radX) * cos(radY);
        viewMatrix = glm::lookAt(camPos, glm::vec3(0,0,0), glm::vec3(0,1,0));
    }

    MoveType mapKeyToMove(SDL_Keycode key, bool shiftPressed) { float angle = fmod(fmod(cameraRotY, 360.0f) + 360.0f, 360.0f); int orientation; if (angle >= 315.0f || angle < 45.0f) orientation = 0; else if (angle >= 45.0f && angle < 135.0f) orientation = 1; else if (angle >= 135.0f && angle < 225.0f) orientation = 2; else orientation = 3; bool topView = cameraRotX > 45.0f; bool bottomView = cameraRotX < -45.0f; static const MoveType horizontalMap[4][6] = { {MOVE_F, MOVE_B, MOVE_R, MOVE_L, MOVE_U, MOVE_D}, {MOVE_L, MOVE_R, MOVE_F, MOVE_B, MOVE_U, MOVE_D}, {MOVE_B, MOVE_F, MOVE_L, MOVE_R, MOVE_U, MOVE_D}, {MOVE_R, MOVE_L, MOVE_B, MOVE_F, MOVE_U, MOVE_D}, }; static const MoveType topViewMap[4][6] = { {MOVE_U, MOVE_D, MOVE_R, MOVE_L, MOVE_B, MOVE_F}, {MOVE_U, MOVE_D, MOVE_F, MOVE_B, MOVE_R, MOVE_L}, {MOVE_U, MOVE_D, MOVE_L, MOVE_R, MOVE_F, MOVE_B}, {MOVE_U, MOVE_D, MOVE_B, MOVE_F, MOVE_L, MOVE_R}, }; static const MoveType bottomViewMap[4][6] = { {MOVE_D, MOVE_U, MOVE_R, MOVE_L, MOVE_F, MOVE_B}, {MOVE_D, MOVE_U, MOVE_B, MOVE_F, MOVE_R, MOVE_L}, {MOVE_D, MOVE_U, MOVE_L, MOVE_R, MOVE_B, MOVE_F}, {MOVE_D, MOVE_U, MOVE_F, MOVE_B, MOVE_L, MOVE_R}, }; int relativeKey = -1; switch (key) { case SDLK_f: relativeKey = 0; break; case SDLK_b: relativeKey = 1; break; case SDLK_r: relativeKey = 2; break; case SDLK_l: relativeKey = 3; break; case SDLK_u: relativeKey = 4; break; case SDLK_d: relativeKey = 5; break; case SDLK_m: return shiftPressed ? MOVE_M_PRIME : MOVE_M; case SDLK_e: return shiftPressed ? MOVE_E_PRIME : MOVE_E; case SDLK_s: return shiftPressed ? MOVE_S_PRIME : MOVE_S; default: return MOVE_NONE; } if (relativeKey == -1) return MOVE_NONE; MoveType baseMove; if (topView) baseMove = topViewMap[orientation][relativeKey]; else if (bottomView) baseMove = bottomViewMap[orientation][relativeKey]; else baseMove = horizontalMap[orientation][relativeKey]; if (shiftPressed) return static_cast<MoveType>(static_cast<int>(baseMove) + 1); return baseMove; }

    void performInstantMove(MoveType move) {
        int axis = 0; int dir = 1; 
        switch (move) { case MOVE_F: axis=2; dir=-1; break; case MOVE_F_PRIME: axis=2; dir=1; break; case MOVE_B: axis=2; dir=1; break; case MOVE_B_PRIME: axis=2; dir=-1; break; case MOVE_L: axis=0; dir=1; break; case MOVE_L_PRIME: axis=0; dir=-1; break; case MOVE_R: axis=0; dir=-1; break; case MOVE_R_PRIME: axis=0; dir=1; break; case MOVE_U: axis=1; dir=-1; break; case MOVE_U_PRIME: axis=1; dir=1; break; case MOVE_D: axis=1; dir=1; break; case MOVE_D_PRIME: axis=1; dir=-1; break; case MOVE_M: axis=0; dir=1; break; case MOVE_M_PRIME: axis=0; dir=-1; break; case MOVE_E: axis=1; dir=1; break; case MOVE_E_PRIME: axis=1; dir=-1; break; case MOVE_S: axis=2; dir=-1; break; case MOVE_S_PRIME: axis=2; dir=1; break; default: return; }
        for (auto& cubie : cubies) {
            bool sel = false;
            switch (move) { case MOVE_F: case MOVE_F_PRIME: sel = (cubie.z == 1); break; case MOVE_B: case MOVE_B_PRIME: sel = (cubie.z == -1); break; case MOVE_L: case MOVE_L_PRIME: sel = (cubie.x == -1); break; case MOVE_R: case MOVE_R_PRIME: sel = (cubie.x == 1); break; case MOVE_U: case MOVE_U_PRIME: sel = (cubie.y == 1); break; case MOVE_D: case MOVE_D_PRIME: sel = (cubie.y == -1); break; case MOVE_M: case MOVE_M_PRIME: sel = (cubie.x == 0); break; case MOVE_E: case MOVE_E_PRIME: sel = (cubie.y == 0); break; case MOVE_S: case MOVE_S_PRIME: sel = (cubie.z == 0); break; default: break; }
            if (sel) { int times = (dir > 0) ? 1 : 3; if (axis == 0) cubie.rotateX(times); else if (axis == 1) cubie.rotateY(times); else cubie.rotateZ(times); }
        }
    }

public:
    RubiksCube(GLuint skyboxTex) : logoTexture(0), skyboxTexture(skyboxTex), animating(false), animationAngle(0), targetAngle(90), animationSpeed(15.0f), cameraRotX(25), cameraRotY(-35), cameraDistance(12.0f), autoSolving(false) {
        for (int x = -1; x <= 1; x++) for (int y = -1; y <= 1; y++) for (int z = -1; z <= 1; z++) cubies.emplace_back(x, y, z);
        logoTexture = loadTexture("textures/logo.png");
        updateMatrices(); 
    }

    void scramble() {
        if (animating || autoSolving) return;
        // FIX: Do NOT clear history. Just append new random moves.
        // This ensures subsequent scrambles are all recorded.
        for (int i = 0; i < 20; i++) {
            int r = rand() % 18;
            MoveType m = static_cast<MoveType>(r);
            performInstantMove(m);
            history.push_back(m);
        }
    }
    
    // The "Solve" function users expect
    void solve() {
        if (animating || autoSolving || history.empty()) return;
        
        autoSolving = true;
        moveQueue.clear();
        
        // Reverse the entire history to return to solved state
        for (int i = history.size() - 1; i >= 0; i--) {
            MoveType m = history[i];
            MoveType inv;
            // Invert the move (Even is Normal, Odd is Prime)
            if (m % 2 == 0) inv = static_cast<MoveType>(m + 1); // F -> F'
            else inv = static_cast<MoveType>(m - 1); // F' -> F
            moveQueue.push_back(inv);
        }
        history.clear();
    }

    void startMove(MoveType move) { 
        if (animating) return; 
        animating = true; currentMove = move; animationAngle = 0; animatingCubies.clear(); originalPositions.clear(); 
        
        // FIX: Record MANUAL moves too, so the solver can undo them
        if (!autoSolving) {
            history.push_back(move);
        }

        switch (move) { case MOVE_F: rotAxis=2; rotDirection=-1; break; case MOVE_F_PRIME: rotAxis=2; rotDirection=1; break; case MOVE_B: rotAxis=2; rotDirection=1; break; case MOVE_B_PRIME: rotAxis=2; rotDirection=-1; break; case MOVE_L: rotAxis=0; rotDirection=1; break; case MOVE_L_PRIME: rotAxis=0; rotDirection=-1; break; case MOVE_R: rotAxis=0; rotDirection=-1; break; case MOVE_R_PRIME: rotAxis=0; rotDirection=1; break; case MOVE_U: rotAxis=1; rotDirection=-1; break; case MOVE_U_PRIME: rotAxis=1; rotDirection=1; break; case MOVE_D: rotAxis=1; rotDirection=1; break; case MOVE_D_PRIME: rotAxis=1; rotDirection=-1; break; case MOVE_M: rotAxis=0; rotDirection=1; break; case MOVE_M_PRIME: rotAxis=0; rotDirection=-1; break; case MOVE_E: rotAxis=1; rotDirection=1; break; case MOVE_E_PRIME: rotAxis=1; rotDirection=-1; break; case MOVE_S: rotAxis=2; rotDirection=-1; break; case MOVE_S_PRIME: rotAxis=2; rotDirection=1; break; default: animating = false; return; } for (size_t i = 0; i < cubies.size(); i++) { bool sel = false; int x = cubies[i].x, y = cubies[i].y, z = cubies[i].z; switch (move) { case MOVE_F: case MOVE_F_PRIME: sel = (z == 1); break; case MOVE_B: case MOVE_B_PRIME: sel = (z == -1); break; case MOVE_L: case MOVE_L_PRIME: sel = (x == -1); break; case MOVE_R: case MOVE_R_PRIME: sel = (x == 1); break; case MOVE_U: case MOVE_U_PRIME: sel = (y == 1); break; case MOVE_D: case MOVE_D_PRIME: sel = (y == -1); break; case MOVE_M: case MOVE_M_PRIME: sel = (x == 0); break; case MOVE_E: case MOVE_E_PRIME: sel = (y == 0); break; case MOVE_S: case MOVE_S_PRIME: sel = (z == 0); break; default: break; } if (sel) { animatingCubies.push_back(i); originalPositions.push_back({x, y, z}); } } }
    
    void update() { 
        if (!animating && !moveQueue.empty()) {
            MoveType m = moveQueue.front();
            moveQueue.pop_front();
            startMove(m);
            return;
        }
        if (moveQueue.empty()) autoSolving = false;
        if (!animating) return; 
        
        float speed = autoSolving ? 25.0f : animationSpeed;
        animationAngle += speed; 
        if (animationAngle >= targetAngle) { 
            for (size_t idx : animatingCubies) { 
                int times = (rotDirection > 0) ? 1 : 3; 
                if (rotAxis == 0) cubies[idx].rotateX(times); 
                else if (rotAxis == 1) cubies[idx].rotateY(times); 
                else cubies[idx].rotateZ(times); 
            } 
            animating = false; 
        } 
    }
    
    void draw(Shader& shader) {
        updateMatrices(); shader.setMat4("projection", projMatrix); shader.setMat4("view", viewMatrix); shader.setVec3("uCamPos", camPos);
        for (size_t i = 0; i < cubies.size(); i++) {
            glm::mat4 model = glm::mat4(1.0f); bool isAnim = false; size_t animIdx = 0; for(size_t k=0; k<animatingCubies.size(); k++) { if(animatingCubies[k] == i) { isAnim=true; animIdx=k; break; } }
            if (isAnim && animating) { float angle = animationAngle * rotDirection; glm::vec3 axis = (rotAxis==0) ? glm::vec3(1,0,0) : ((rotAxis==1) ? glm::vec3(0,1,0) : glm::vec3(0,0,1)); model = glm::rotate(model, glm::radians(angle), axis); model = glm::translate(model, glm::vec3(originalPositions[animIdx][0], originalPositions[animIdx][1], originalPositions[animIdx][2])); } 
            else { model = glm::translate(model, glm::vec3(cubies[i].x, cubies[i].y, cubies[i].z)); }
            cubies[i].draw(shader, mesh, model, logoTexture, skyboxTexture);
        }
    }
    void rotateCamera(int dx, int dy) { cameraRotY += dx * 0.5f; cameraRotX += dy * 0.5f; cameraRotX = glm::clamp(cameraRotX, -89.0f, 89.0f); updateMatrices(); }
    void zoom(int dir) { cameraDistance -= dir * 1.0f; cameraDistance = glm::clamp(cameraDistance, 6.0f, 25.0f); updateMatrices(); }
    void handleKeyPress(SDL_Keycode key, bool shift) { MoveType move = mapKeyToMove(key, shift); if (move != MOVE_NONE) startMove(move); }
    bool pickCubie(int mouseX, int mouseY, int& outIndex, int& outFace) { updateMatrices(); glm::vec4 viewport = glm::vec4(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT); glm::vec3 winCoords = glm::vec3(mouseX, WINDOW_HEIGHT - mouseY, 0.0f); glm::vec3 nearPos = glm::unProject(glm::vec3(winCoords.x, winCoords.y, 0.0f), viewMatrix, projMatrix, viewport); glm::vec3 farPos = glm::unProject(glm::vec3(winCoords.x, winCoords.y, 1.0f), viewMatrix, projMatrix, viewport); glm::vec3 rayDir = glm::normalize(farPos - nearPos); glm::vec3 rayOrigin = nearPos; float minT = 1000.0f; int bestIdx = -1; int bestFace = -1; for(size_t i=0; i<cubies.size(); ++i) { glm::vec3 pos(cubies[i].x, cubies[i].y, cubies[i].z); for(int f=0; f<6; ++f) { if(cubies[i].stickers[f] == BLACK_PLASTIC) continue; glm::vec3 normal(0); if(f==POS_X) normal.x=1; else if(f==NEG_X) normal.x=-1; else if(f==POS_Y) normal.y=1; else if(f==NEG_Y) normal.y=-1; else if(f==POS_Z) normal.z=1; else if(f==NEG_Z) normal.z=-1; glm::vec3 faceCenter = pos + normal * 0.5f; float denom = glm::dot(normal, rayDir); if(fabs(denom) < 1e-6) continue; float t = glm::dot(faceCenter - rayOrigin, normal) / denom; if(t < 0 || t > minT) continue; glm::vec3 hitPoint = rayOrigin + t * rayDir; glm::vec3 localHit = hitPoint - pos; bool hit = false; float s = 0.5f; if (f < 2) hit = (fabs(localHit.y) <= s && fabs(localHit.z) <= s); else if (f < 4) hit = (fabs(localHit.x) <= s && fabs(localHit.z) <= s); else hit = (fabs(localHit.x) <= s && fabs(localHit.y) <= s); if(hit) { minT = t; bestIdx = i; bestFace = f; } } } if (bestIdx != -1) { outIndex = bestIdx; outFace = bestFace; return true; } return false; }
    MoveType getMoveFromDrag(int faceDir, int cubieIndex, int dragDX, int dragDY) { if (cubieIndex < 0 || cubieIndex >= (int)cubies.size()) return MOVE_NONE; glm::mat4 invView = glm::inverse(viewMatrix); glm::vec3 camRight = glm::vec3(invView[0]); glm::vec3 camUp = glm::vec3(invView[1]); glm::vec3 worldDrag = (float)dragDX * camRight - (float)dragDY * camUp; float dragH = 0, dragV = 0; switch (faceDir) { case POS_Z: dragH = worldDrag.x; dragV = worldDrag.y; break; case NEG_Z: dragH = -worldDrag.x; dragV = worldDrag.y; break; case POS_X: dragH = -worldDrag.z; dragV = worldDrag.y; break; case NEG_X: dragH = worldDrag.z; dragV = worldDrag.y; break; case POS_Y: dragH = worldDrag.x; dragV = -worldDrag.z; break; case NEG_Y: dragH = worldDrag.x; dragV = worldDrag.z; break; } bool horizontal = fabs(dragH) > fabs(dragV); int dirH = (dragH > 0) ? 1 : -1; int dirV = (dragV > 0) ? 1 : -1; int cx = cubies[cubieIndex].x, cy = cubies[cubieIndex].y, cz = cubies[cubieIndex].z; if (faceDir == POS_Z || faceDir == NEG_Z) { if (horizontal) { if (cy == 1) return (dirH > 0) ? MOVE_U_PRIME : MOVE_U; if (cy == -1) return (dirH > 0) ? MOVE_D : MOVE_D_PRIME; return (dirH > 0) ? MOVE_E : MOVE_E_PRIME; } else { MoveType m; if (cx == 1) m = (dirV > 0) ? MOVE_R : MOVE_R_PRIME; else if (cx == -1) m = (dirV > 0) ? MOVE_L_PRIME : MOVE_L; else m = (dirV > 0) ? MOVE_M_PRIME : MOVE_M; if (faceDir == NEG_Z) { if (m == MOVE_R) m = MOVE_R_PRIME; else if (m == MOVE_R_PRIME) m = MOVE_R; if (m == MOVE_L) m = MOVE_L_PRIME; else if (m == MOVE_L_PRIME) m = MOVE_L; if (m == MOVE_M) m = MOVE_M_PRIME; else if (m == MOVE_M_PRIME) m = MOVE_M; } return m; } } else if (faceDir == POS_X || faceDir == NEG_X) { if (horizontal) { if (cy == 1) return (dirH > 0) ? MOVE_U_PRIME : MOVE_U; if (cy == -1) return (dirH > 0) ? MOVE_D : MOVE_D_PRIME; return (dirH > 0) ? MOVE_E : MOVE_E_PRIME; } else { MoveType m; if (cz == 1) m = (dirV > 0) ? MOVE_F_PRIME : MOVE_F; else if (cz == -1) m = (dirV > 0) ? MOVE_B : MOVE_B_PRIME; else m = (dirV > 0) ? MOVE_S_PRIME : MOVE_S; if (faceDir == NEG_X) { if (m == MOVE_F) m = MOVE_F_PRIME; else if (m == MOVE_F_PRIME) m = MOVE_F; if (m == MOVE_B) m = MOVE_B_PRIME; else if (m == MOVE_B_PRIME) m = MOVE_B; if (m == MOVE_S) m = MOVE_S_PRIME; else if (m == MOVE_S_PRIME) m = MOVE_S; } return m; } } else { if (horizontal) { MoveType m; if (cz == 1) m = (dirH > 0) ? MOVE_F : MOVE_F_PRIME; else if (cz == -1) m = (dirH > 0) ? MOVE_B_PRIME : MOVE_B; else m = (dirH > 0) ? MOVE_S : MOVE_S_PRIME; if (faceDir == NEG_Y) { if (m == MOVE_F) m = MOVE_F_PRIME; else if (m == MOVE_F_PRIME) m = MOVE_F; if (m == MOVE_B) m = MOVE_B_PRIME; else if (m == MOVE_B_PRIME) m = MOVE_B; if (m == MOVE_S) m = MOVE_S_PRIME; else if (m == MOVE_S_PRIME) m = MOVE_S; } return m; } else { MoveType m; if (cx == 1) m = (dirV > 0) ? MOVE_R : MOVE_R_PRIME; else if (cx == -1) m = (dirV > 0) ? MOVE_L_PRIME : MOVE_L; else m = (dirV > 0) ? MOVE_M_PRIME : MOVE_M; if (faceDir == NEG_Y) { if (m == MOVE_R) m = MOVE_R_PRIME; else if (m == MOVE_R_PRIME) m = MOVE_R; if (m == MOVE_L) m = MOVE_L_PRIME; else if (m == MOVE_L_PRIME) m = MOVE_L; if (m == MOVE_M) m = MOVE_M_PRIME; else if (m == MOVE_M_PRIME) m = MOVE_M; } return m; } } }
    void getMatrices(glm::mat4& v, glm::mat4& p) { v = viewMatrix; p = projMatrix; }
};

int main(int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) { std::cerr << "SDL Init Failed: " << SDL_GetError() << std::endl; return 1; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1); SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

    SDL_Window* window = SDL_CreateWindow("Rubiks PBR (Fixed Solver)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    if (!window) { std::cerr << "Window Create Failed: " << SDL_GetError() << std::endl; return 1; }
    
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context) { std::cerr << "Context Create Failed: " << SDL_GetError() << std::endl; return 1; }
    
    glewExperimental = GL_TRUE; 
    if (glewInit() != GLEW_OK) { std::cerr << "GLEW Init Failed" << std::endl; return 1; }

    glEnable(GL_DEPTH_TEST); glEnable(GL_MULTISAMPLE); glEnable(GL_FRAMEBUFFER_SRGB); 
    std::srand(std::time(nullptr));

    Shader cubeShader(cubeVS, cubeFS);
    Shader skyboxShader(skyboxVS, skyboxFS);
    SkyboxMesh skyboxMesh;

    std::vector<std::string> faces = { "textures/right.png", "textures/left.png", "textures/top.png", "textures/bottom.png", "textures/front.png", "textures/back.png" };
    GLuint skyboxTex = loadCubemap(faces);

    RubiksCube cube(skyboxTex);

    bool running = true; SDL_Event event;
    bool rightDown = false; bool leftDown = false; int lastX=0, lastY=0; int clickStartX=0, clickStartY=0; int pickedCubie=-1, pickedFace=-1; bool draggingCube=false;

    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
            else if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_ESCAPE) running = false;
                if (event.key.keysym.sym == SDLK_SPACE) cube.scramble();
                else if (event.key.keysym.sym == SDLK_c) cube.solve(); // CHANGED TO C
                else cube.handleKeyPress(event.key.keysym.sym, (event.key.keysym.mod & KMOD_SHIFT));
            }
            else if (event.type == SDL_MOUSEBUTTONDOWN) {
                if (event.button.button == SDL_BUTTON_RIGHT) { rightDown = true; lastX = event.button.x; lastY = event.button.y; }
                else if (event.button.button == SDL_BUTTON_LEFT) { leftDown = true; clickStartX = event.button.x; clickStartY = event.button.y; draggingCube = false; if (!cube.pickCubie(clickStartX, clickStartY, pickedCubie, pickedFace)) pickedCubie = -1; }
            }
            else if (event.type == SDL_MOUSEBUTTONUP) {
                if (event.button.button == SDL_BUTTON_RIGHT) rightDown = false;
                if (event.button.button == SDL_BUTTON_LEFT) { leftDown = false; draggingCube = false; pickedCubie = -1; }
            }
            else if (event.type == SDL_MOUSEMOTION) {
                if (rightDown) { cube.rotateCamera(event.motion.x - lastX, event.motion.y - lastY); lastX = event.motion.x; lastY = event.motion.y; }
                else if (leftDown && pickedCubie != -1) {
                    int dx = event.motion.x - clickStartX; int dy = event.motion.y - clickStartY;
                    if (!draggingCube && (dx*dx + dy*dy > MIN_DRAG_DISTANCE*MIN_DRAG_DISTANCE)) {
                         MoveType move = cube.getMoveFromDrag(pickedFace, pickedCubie, dx, dy);
                         if (move != MOVE_NONE) { cube.startMove(move); draggingCube = true; }
                    }
                }
            }
            else if (event.type == SDL_MOUSEWHEEL) cube.zoom(event.wheel.y);
        }

        cube.update();
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 view, proj;
        cube.getMatrices(view, proj);
        cubeShader.use();
        cube.draw(cubeShader);

        glDepthFunc(GL_LEQUAL);
        skyboxShader.use();
        skyboxShader.setMat4("view", glm::mat4(glm::mat3(view))); 
        skyboxShader.setMat4("projection", proj);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_CUBE_MAP, skyboxTex);
        skyboxMesh.draw();
        glDepthFunc(GL_LESS);

        SDL_GL_SwapWindow(window);
    }
    SDL_GL_DeleteContext(context); SDL_DestroyWindow(window); SDL_Quit();
    return 0;
}