#include <SDL2/SDL.h>
#include <GLES2/gl2.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <thread>
#include <chrono>

bool fullscreen = false;
SDL_Window* window = nullptr;
SDL_GLContext glContext = nullptr;
int windowedWidth = 800, windowedHeight = 600;
int frames = 0;
const double targetFrameTime = 1.0 / 50.0;
double lastTime = 0;
bool Fkey = false;

// Cube vertex and index data
GLfloat vertices[] = {
    -0.5f, -0.5f, -0.5f, 1, 0, 0,  // 0
     0.5f, -0.5f, -0.5f, 0, 1, 0,  // 1
     0.5f,  0.5f, -0.5f, 0, 0, 1,  // 2
    -0.5f,  0.5f, -0.5f, 1, 1, 0,  // 3
    -0.5f, -0.5f,  0.5f, 1, 0, 1,  // 4
     0.5f, -0.5f,  0.5f, 0, 1, 1,  // 5
     0.5f,  0.5f,  0.5f, 1, 1, 1,  // 6
    -0.5f,  0.5f,  0.5f, 0, 0, 0   // 7
};

GLushort indices[] = {
    0,1,2, 2,3,0,  4,5,6, 6,7,4,
    4,5,1, 1,0,4,  3,2,6, 6,7,3,
    4,0,3, 3,7,4,  1,5,6, 6,2,1
};

void timer() {
    int count = frames;
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        int newcount = frames;
        std::cout << "FPS: " << (newcount - count) << std::endl;
        count = newcount;
    }
}

GLuint compileShader(GLenum type, const char* src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, 512, nullptr, log);
        std::cerr << "Shader error: " << log << std::endl;
    }
    return shader;
}

GLuint createShaderProgram(const char* vs, const char* fs) {
    GLuint vertex = compileShader(GL_VERTEX_SHADER, vs);
    GLuint fragment = compileShader(GL_FRAGMENT_SHADER, fs);
    GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    return program;
}

void toggleFullscreen() {
    fullscreen = !fullscreen;
    SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

int main() {
    std::thread(timer).detach();

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL init failed\n";
        return -1;
    }

    // GLES2 context setup
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    window = SDL_CreateWindow("Cube SDL2 + GLES2", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              windowedWidth, windowedHeight, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::cerr << "Window creation failed!\n";
        return -1;
    }

    glContext = SDL_GL_CreateContext(window);
    SDL_GL_SetSwapInterval(0);  // No vsync

    glEnable(GL_DEPTH_TEST);

    const char* vertexShader = R"(
        precision mediump float;
        attribute vec3 position;
        attribute vec3 color;
        varying vec3 fragColor;
        uniform mat4 model;
        uniform mat4 view;
        uniform mat4 projection;
        void main() {
            fragColor = color;
            gl_Position = projection * view * model * vec4(position, 1.0);
        })";

    const char* fragmentShader = R"(
        precision mediump float;
        varying vec3 fragColor;
        void main() {
            gl_FragColor = vec4(fragColor, 1.0);
        })";

    GLuint program = createShaderProgram(vertexShader, fragmentShader);

    GLuint VBO, EBO;
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    GLint posAttrib = glGetAttribLocation(program, "position");
    GLint colAttrib = glGetAttribLocation(program, "color");
    GLuint modelLoc = glGetUniformLocation(program, "model");
    GLuint viewLoc = glGetUniformLocation(program, "view");
    GLuint projLoc = glGetUniformLocation(program, "projection");

    bool running = true;
    SDL_Event event;

    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_ESCAPE) running = false;
                if (event.key.keysym.sym == SDLK_f && !Fkey) {
                    Fkey = true;
                    toggleFullscreen();
                }
            }
            if (event.type == SDL_KEYUP && event.key.keysym.sym == SDLK_f) Fkey = false;
        }

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUseProgram(program);

        float angle = frames * 0.3f;
        glm::mat4 model = glm::rotate(glm::mat4(1), glm::radians(angle), glm::vec3(1, 1, 0));
        glm::mat4 view = glm::translate(glm::mat4(1), glm::vec3(0, 0, -3));
        glm::mat4 projection = glm::perspective(glm::radians(45.0f), windowedWidth / (float)windowedHeight, 0.1f, 100.0f);

        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));

        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glEnableVertexAttribArray(posAttrib);
        glVertexAttribPointer(posAttrib, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), (GLvoid*)0);
        glEnableVertexAttribArray(colAttrib);
        glVertexAttribPointer(colAttrib, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), (GLvoid*)(3 * sizeof(GLfloat)));

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_SHORT, 0);

        double currentTime = SDL_GetTicks() / 1000.0;
        double elapsed = currentTime - lastTime;
        if (elapsed < targetFrameTime)
            std::this_thread::sleep_for(std::chrono::duration<double>(targetFrameTime - elapsed));

        lastTime = currentTime;
        SDL_GL_SwapWindow(window);
        frames++;
    }

    glDeleteBuffers(1, &VBO);
    glDeleteBuffers(1, &EBO);
    glDeleteProgram(program);
    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
