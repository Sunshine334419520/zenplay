#pragma once

class Renderer {
public:
    static Renderer* CreateRenderer();

    Renderer() = default;
    virtual ~Renderer() = default;

    // Initialize the renderer
    virtual void Init() = 0;

    // Render a frame
    virtual void RenderFrame() = 0;

    // Cleanup resources
    virtual void OnResize() = 0;

    // Resize the viewport
    virtual void resize(int width, int height) = 0;
};