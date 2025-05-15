#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <thread>
#include <chrono>

// Window state
bool fullscreen = false;
GLFWwindow* window;
int windowedX, windowedY, windowedWidth = 800, windowedHeight = 600;
volatile int frames = 0;
const double targetFrameTime = 1.0 / 60.0;  // 50 FPS → 20 ms
double lastTime = 0;

// Vertex data: 8 vertices, position + color
GLfloat vertices[] = {
    // Position           // Color
    -0.5f, -0.5f, -0.5f,  1.0f, 0.0f, 0.0f, // 0
     0.5f, -0.5f, -0.5f,  0.0f, 1.0f, 0.0f, // 1
     0.5f,  0.5f, -0.5f,  0.0f, 0.0f, 1.0f, // 2
    -0.5f,  0.5f, -0.5f,  1.0f, 1.0f, 0.0f, // 3
    -0.5f, -0.5f,  0.5f,  1.0f, 0.0f, 1.0f, // 4
     0.5f, -0.5f,  0.5f,  0.0f, 1.0f, 1.0f, // 5
     0.5f,  0.5f,  0.5f,  1.0f, 1.0f, 1.0f, // 6
    -0.5f,  0.5f,  0.5f,  0.0f, 0.0f, 0.0f  // 7
};

void timer() {
    int count = frames;
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));  // Wait for 1 second
        int newcount = frames;
        std::cout << "FPS: "<< (newcount-count) << std::endl;
        count = newcount;
    }
}

GLuint indices[] = {
    0, 1, 2, 2, 3, 0, // Front
    4, 5, 6, 6, 7, 4, // Back
    4, 5, 1, 1, 0, 4, // Bottom
    3, 2, 6, 6, 7, 3, // Top
    4, 0, 3, 3, 7, 4, // Left
    1, 5, 6, 6, 2, 1  // Right
};

GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, 512, nullptr, log);
        std::cerr << "Shader Compilation Failed: " << log << std::endl;
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

    if (fullscreen) {
        glfwGetWindowPos(window, &windowedX, &windowedY);
        glfwGetWindowSize(window, &windowedWidth, &windowedHeight);

        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
    } else {
        glfwSetWindowMonitor(window, nullptr, windowedX, windowedY, windowedWidth, windowedHeight, 0);
    }
}

int main() {

    std::thread timerThread(timer);  // Create a thread to run the timer
    timerThread.detach();  // Detach the thread so it runs independently

    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    window = glfwCreateWindow(windowedWidth, windowedHeight, "Rotating Cube", nullptr, nullptr);
    if (!window) {
        std::cerr << "Window creation failed!" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    //glfwSwapInterval(1);  // 1 enables V-Sync, 0 disables V-Sync (unlimited FPS)

    glewExperimental = true;
    if (glewInit() != GLEW_OK) {
        std::cerr << "GLEW init failed!" << std::endl;
        return -1;
    }

    glEnable(GL_DEPTH_TEST);

    const char* vertexShader = R"(
        #version 330 core
        layout(location = 0) in vec3 position;
        layout(location = 1) in vec3 color;
        out vec3 fragColor;
        uniform mat4 model;
        uniform mat4 view;
        uniform mat4 projection;
        void main() {
            fragColor = color;
            gl_Position = projection * view * model * vec4(position, 1.0);
        })";

    const char* fragmentShader = R"(
        #version 330 core
        in vec3 fragColor;
        out vec4 outColor;
        void main() {
            outColor = vec4(fragColor, 1.0);
        })";

    GLuint program = createShaderProgram(vertexShader, fragmentShader);

    GLuint VAO, VBO, EBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), (GLvoid*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), (GLvoid*)(3 * sizeof(GLfloat)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    GLuint modelLoc = glGetUniformLocation(program, "model");
    GLuint viewLoc = glGetUniformLocation(program, "view");
    GLuint projLoc = glGetUniformLocation(program, "projection");

    while (!glfwWindowShouldClose(window)) {
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);
        if (glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS)
            toggleFullscreen();

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUseProgram(program);

        float time = glfwGetTime();
        glm::mat4 model = glm::rotate(glm::mat4(1.0f), glm::radians(frames * 0.3f), glm::vec3(1.0f, 1.0f, 0.0f));
        glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, -3));
        glm::mat4 projection = glm::perspective(glm::radians(45.0f), 800.f / 600.f, 0.1f, 100.0f);

        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));

        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);

        double currentTime = glfwGetTime();
        double elapsed = currentTime - lastTime;

        if (elapsed < targetFrameTime) {
            // Sleep just enough to reach 20 ms total
            std::this_thread::sleep_for(
                std::chrono::duration<double>(targetFrameTime - elapsed));
            lastTime += targetFrameTime;
        }
        else if (elapsed > targetFrameTime)
        {
            lastTime = currentTime;
        }


        glfwSwapBuffers(window);
        glfwPollEvents();
        frames++;
    }

    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteBuffers(1, &EBO);
    glDeleteProgram(program);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
