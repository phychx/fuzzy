package com.wiranoid.fuzzy;

import org.lwjgl.BufferUtils;
import org.lwjgl.glfw.GLFWErrorCallback;
import org.lwjgl.glfw.GLFWKeyCallback;
import org.lwjgl.glfw.GLFWVidMode;
import org.lwjgl.opengl.GL;

import java.nio.FloatBuffer;
import java.nio.IntBuffer;

import static org.lwjgl.glfw.GLFW.*;
import static org.lwjgl.opengl.GL11.*;
import static org.lwjgl.opengl.GL15.*;
import static org.lwjgl.opengl.GL20.*;
import static org.lwjgl.opengl.GL30.*;
import static org.lwjgl.system.MemoryUtil.NULL;


public class HelloFuzzy {

    private static GLFWErrorCallback errorCallback
            = GLFWErrorCallback.createPrint(System.err);

    // Is called whenever a key is pressed/released via GLFW
    private static GLFWKeyCallback keyCallback = new GLFWKeyCallback() {
        @Override
        public void invoke(long window, int key, int scancode, int action, int mods) {
            if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
                glfwSetWindowShouldClose(window, true);
            }
        }
    };

    private static int WIDTH = 640;
    private static int HEIGHT = 480;

    // Shaders
    private static String vertexShaderSource = "#version 330 core\n" +
            "in vec3 position;\n" +
            "void main() {\n" +
                "gl_Position = vec4(position, 1.0f);\n" +
            "}\n";

    private static String fragmentShaderSource = "#version 330 core\n" +
            "out vec4 color;\n" +
            "uniform vec4 ourColor;\n" +
            "void main() {\n" +
                "color = ourColor;\n" +
            "}\n";

    public static void main(String[] args) {
        glfwSetErrorCallback(errorCallback);

        if (!glfwInit()) {
            throw new IllegalStateException("Unable to initialize GLFW");
        }

        // Set all the required options for GLFW
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);

        long window = glfwCreateWindow(WIDTH, HEIGHT, "Fuzzy", NULL, NULL);
        if (window == NULL) {
            glfwTerminate();
            throw new RuntimeException("Failed to create GLFW window");
        }

        GLFWVidMode vidMode = glfwGetVideoMode(glfwGetPrimaryMonitor());
        glfwSetWindowPos(window,
                (vidMode.width() - WIDTH) / 2,
                (vidMode.height() - HEIGHT) / 2);

        glfwSetKeyCallback(window, keyCallback);

        glfwMakeContextCurrent(window);
        GL.createCapabilities();

        // Define the viewport dimensions
        IntBuffer width = BufferUtils.createIntBuffer(1);
        IntBuffer height = BufferUtils.createIntBuffer(1);
        glfwGetFramebufferSize(window, width, height);
        glViewport(0, 0, width.get(), height.get());

        // Build and compile our shader program
        // Vertex shader
        int vertexShader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertexShader, vertexShaderSource);
        glCompileShader(vertexShader);

        // Check for compile time errors
        int status = glGetShaderi(vertexShader, GL_COMPILE_STATUS);
        if (status != GL_TRUE) {
            throw new RuntimeException(glGetShaderInfoLog(vertexShader));
        }

        // Fragment shader
        int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragmentShader, fragmentShaderSource);
        glCompileShader(fragmentShader);

        // Check for compile time errors
        status = glGetShaderi(fragmentShader, GL_COMPILE_STATUS);
        if (status != GL_TRUE) {
            throw new RuntimeException(glGetShaderInfoLog(fragmentShader));
        }

        // Link shaders
        int shaderProgram = glCreateProgram();
        glAttachShader(shaderProgram, vertexShader);
        glAttachShader(shaderProgram, fragmentShader);
        glLinkProgram(shaderProgram);

        // Check for linking errors
        status = glGetProgrami(shaderProgram, GL_LINK_STATUS);
        if (status != GL_TRUE) {
            throw new RuntimeException(glGetProgramInfoLog(shaderProgram));
        }

        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);

        // Bind the Vertex Array Object first, then bind and set vertex buffer(s) and attribute pointer(s).
        int VAO = glGenVertexArrays();
        glBindVertexArray(VAO);

        FloatBuffer vertices = BufferUtils.createFloatBuffer(3 * 4);
        // Top Right
        vertices.put(0.5f).put(0.5f).put(0.0f);
        // Bottom Right
        vertices.put(0.5f).put(-0.5f).put(0.0f);
        // Bottom Left
        vertices.put(-0.5f).put(-0.5f).put(0.0f);
        // Top Left
        vertices.put(-0.5f).put(0.5f).put(0.0f);
        // Passing the buffer without flipping will crash JVM because of a EXCEPTION_ACCESS_VIOLATION
        vertices.flip();

        int VBO = glGenBuffers();
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, vertices, GL_STATIC_DRAW);

        IntBuffer indices = BufferUtils.createIntBuffer(2 * 3);
        // First Triangle
        indices.put(0).put(1).put(3);
        // Second Triangle
        indices.put(1).put(2).put(3);
        // Passing the buffer without flipping will crash JVM because of a EXCEPTION_ACCESS_VIOLATION
        indices.flip();

        int EBO = glGenBuffers();
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices, GL_STATIC_DRAW);

        // We need to specify the input to our vertex shader
        int posAttrib = glGetAttribLocation(shaderProgram, "position");
        glEnableVertexAttribArray(posAttrib);
        glVertexAttribPointer(posAttrib, 3, GL_FLOAT, false, 3 * Float.BYTES, 0);

        // This is allowed, the call to glVertexAttribPointer registered VBO as the currently bound
        // vertex buffer object so afterwards we can safely unbind
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        // Unbind VAO (it's always a good thing to unbind any buffer/array to prevent strange bugs),
        // remember: do NOT unbind the EBO, keep it bound to this VAO
        glBindVertexArray(0);

        // Enable wireframe polygons
        //glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

        // Game loop
        while (!glfwWindowShouldClose(window)) {
            // Check if any events have been activated (key pressed, mouse moved etc.)
            // and call corresponding response functions
            glfwPollEvents();

            // Render
            // Clear the colorbuffer
            glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            // Activate the shader
            glUseProgram(shaderProgram);

            //update the uniform color
            double timeValue = glfwGetTime();
            double greenValue = (Math.sin(timeValue) / 2) + 0.5;
            int ourColorLocation = glGetUniformLocation(shaderProgram, "ourColor");
            glUniform4f(ourColorLocation, 0.0f, (float) greenValue, 0.0f, 1.0f);

            // Draw our first rectangle (or two triangles)
            glBindVertexArray(VAO);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
            glBindVertexArray(0);

            // Swap the screen buffers
            glfwSwapBuffers(window);
        }

        // Properly de-allocate all resources once they've outlived their purpose
        glDeleteVertexArrays(VAO);
        glDeleteBuffers(VBO);
        glDeleteProgram(shaderProgram);

        glfwDestroyWindow(window);
        keyCallback.free();

        // Terminate GLFW, clearing any resources allocated by GLFW
        glfwTerminate();
        errorCallback.free();
    }

}
