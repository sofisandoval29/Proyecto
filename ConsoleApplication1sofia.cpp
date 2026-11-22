
#define NOMINMAX

#include <Windows.h>
#include <tchar.h>
#include <d3d11.h>
#include <dxgi.h>

#include <vector>
#include <cmath>
#include <algorithm>
#include <string>

#include "IMGUI/imgui.h"
#include "IMGUI/imgui_impl_win32.h"
#include "IMGUI/imgui_impl_dx11.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam
);


static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

static void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer = nullptr;

    if (SUCCEEDED(g_pSwapChain->GetBuffer(
        0, IID_PPV_ARGS(&pBackBuffer))))
    {
        g_pd3dDevice->CreateRenderTargetView(
            pBackBuffer, nullptr, &g_mainRenderTargetView
        );

        pBackBuffer->Release();
    }
}

static void CleanupRenderTarget()
{
    if (g_mainRenderTargetView)
    {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}

static bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;

    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] =
    {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0
    };

    HRESULT res = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        createDeviceFlags,
        featureLevelArray,
        2,
        D3D11_SDK_VERSION,
        &sd,
        &g_pSwapChain,
        &g_pd3dDevice,
        &featureLevel,
        &g_pd3dDeviceContext
    );

    if (res == DXGI_ERROR_UNSUPPORTED)
    {
        res = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            createDeviceFlags,
            featureLevelArray,
            2,
            D3D11_SDK_VERSION,
            &sd,
            &g_pSwapChain,
            &g_pd3dDevice,
            &featureLevel,
            &g_pd3dDeviceContext
        );
    }

    if (res != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

static void CleanupDeviceD3D()
{
    CleanupRenderTarget();

    if (g_pSwapChain)
    {
        g_pSwapChain->Release();
        g_pSwapChain = nullptr;
    }

    if (g_pd3dDeviceContext)
    {
        g_pd3dDeviceContext->Release();
        g_pd3dDeviceContext = nullptr;
    }

    if (g_pd3dDevice)
    {
        g_pd3dDevice->Release();
        g_pd3dDevice = nullptr;
    }
}



const float CELL_SIZE = 20.0f;
const float PI = 3.14159265358979323846f;

enum Tool
{
    TOOL_SELECT,
    TOOL_LINE,
    TOOL_CIRCLE,
    TOOL_FREEHAND,
    TOOL_TRIANGLE,
    TOOL_SQUARE
};

enum ShapeType
{
    SHAPE_LINE,
    SHAPE_CIRCLE,
    SHAPE_FREEHAND,
    SHAPE_TRIANGLE,
    SHAPE_SQUARE
};

struct Point
{
    float x = 0;
    float y = 0;
};

struct Shape
{
    ShapeType type;
    std::vector<Point> points;

    float radius = 0;

    
    float offsetX = 0;
    float offsetY = 0;
    float rotation = 0;
    float scale = 1.0f;

    ImU32 color = IM_COL32(220, 50, 50, 255);
    bool filled = false;
};

std::vector<Shape> shapes;

Tool currentTool = TOOL_SELECT;

int selectedShape = -1;

ImU32 currentColor = IM_COL32(220, 50, 50, 255);
bool fillEnabled = false;

std::vector<Point> pendingPoints;

bool isDrawingFreehand = false;
bool isDraggingShape = false;

ImVec2 lastMousePosition(0, 0);



Point MouseToGrid(ImVec2 mouse, ImVec2 origin)
{
    Point p;

    p.x = floorf((mouse.x - origin.x) / CELL_SIZE);
    p.y = floorf((mouse.y - origin.y) / CELL_SIZE);

    return p;
}

ImVec2 GridToScreen(Point p, ImVec2 origin)
{
    return ImVec2(
        origin.x + p.x * CELL_SIZE,
        origin.y + p.y * CELL_SIZE
    );
}

ImVec2 GridCenterToScreen(Point p, ImVec2 origin)
{
    return ImVec2(
        origin.x + (p.x + 0.5f) * CELL_SIZE,
        origin.y + (p.y + 0.5f) * CELL_SIZE
    );
}

Point GetShapeCenter(const Shape& s)
{
    Point center;

    if (s.points.empty())
        return center;

    for (const Point& p : s.points)
    {
        center.x += p.x;
        center.y += p.y;
    }

    center.x /= (float)s.points.size();
    center.y /= (float)s.points.size();

    return center;
}



Point TransformPoint(const Shape& s, Point p)
{
    Point center = GetShapeCenter(s);

    float dx = (p.x - center.x) * s.scale;
    float dy = (p.y - center.y) * s.scale;

    float radians = s.rotation * PI / 180.0f;

    float cosA = cosf(radians);
    float sinA = sinf(radians);

    Point result;

    result.x = center.x + dx * cosA - dy * sinA + s.offsetX;
    result.y = center.y + dx * sinA + dy * cosA + s.offsetY;

    return result;
}

Point TransformCircleCenter(const Shape& s)
{
    Point center = s.points[0];

    center.x += s.offsetX;
    center.y += s.offsetY;

    return center;
}

std::vector<Point> GetTransformedPoints(const Shape& s)
{
    std::vector<Point> result;

    for (const Point& p : s.points)
        result.push_back(TransformPoint(s, p));

    return result;
}


void DrawCell(
    ImDrawList* draw,
    ImVec2 origin,
    int x,
    int y,
    ImU32 color
)
{
    ImVec2 a(
        origin.x + x * CELL_SIZE,
        origin.y + y * CELL_SIZE
    );

    ImVec2 b(
        a.x + CELL_SIZE,
        a.y + CELL_SIZE
    );

    draw->AddRectFilled(a, b, color);
}



void Bresenham(
    ImDrawList* draw,
    ImVec2 origin,
    int x1,
    int y1,
    int x2,
    int y2,
    ImU32 color
)
{
    int dx = abs(x2 - x1);
    int dy = abs(y2 - y1);

    int sx = x1 < x2 ? 1 : -1;
    int sy = y1 < y2 ? 1 : -1;

    int error = dx - dy;

    while (true)
    {
        DrawCell(draw, origin, x1, y1, color);

        if (x1 == x2 && y1 == y2)
            break;

        int e2 = 2 * error;

        if (e2 > -dy)
        {
            error -= dy;
            x1 += sx;
        }

        if (e2 < dx)
        {
            error += dx;
            y1 += sy;
        }
    }
}



void DrawCirclePoints(
    ImDrawList* draw,
    ImVec2 origin,
    int cx,
    int cy,
    int x,
    int y,
    ImU32 color
)
{
    DrawCell(draw, origin, cx + x, cy + y, color);
    DrawCell(draw, origin, cx - x, cy + y, color);
    DrawCell(draw, origin, cx + x, cy - y, color);
    DrawCell(draw, origin, cx - x, cy - y, color);

    DrawCell(draw, origin, cx + y, cy + x, color);
    DrawCell(draw, origin, cx - y, cy + x, color);
    DrawCell(draw, origin, cx + y, cy - x, color);
    DrawCell(draw, origin, cx - y, cy - x, color);
}

void MidpointCircle(
    ImDrawList* draw,
    ImVec2 origin,
    int cx,
    int cy,
    int radius,
    ImU32 color
)
{
    int x = 0;
    int y = radius;
    int decision = 1 - radius;

    while (x <= y)
    {
        DrawCirclePoints(
            draw, origin, cx, cy, x, y, color
        );

        x++;

        if (decision < 0)
        {
            decision += 2 * x + 1;
        }
        else
        {
            y--;
            decision += 2 * (x - y) + 1;
        }
    }
}



void FillCircle(
    ImDrawList* draw,
    ImVec2 origin,
    int cx,
    int cy,
    int radius,
    ImU32 color
)
{
    for (int y = -radius; y <= radius; y++)
    {
        for (int x = -radius; x <= radius; x++)
        {
            if (x * x + y * y <= radius * radius)
            {
                DrawCell(
                    draw, origin,
                    cx + x, cy + y, color
                );
            }
        }
    }
}



void DrawPolygon(
    ImDrawList* draw,
    ImVec2 origin,
    const std::vector<Point>& points,
    ImU32 color,
    bool filled
)
{
    if (points.size() < 3)
        return;

    std::vector<ImVec2> screenPoints;

    for (const Point& p : points)
    {
        screenPoints.push_back(
            GridCenterToScreen(p, origin)
        );
    }

    if (filled)
    {
        draw->AddConvexPolyFilled(
            screenPoints.data(),
            (int)screenPoints.size(),
            color
        );
    }

    for (size_t i = 0; i < points.size(); i++)
    {
        size_t next = (i + 1) % points.size();

        Bresenham(
            draw,
            origin,
            (int)roundf(points[i].x),
            (int)roundf(points[i].y),
            (int)roundf(points[next].x),
            (int)roundf(points[next].y),
            color
        );
    }
}



void DrawShape(
    ImDrawList* draw,
    ImVec2 origin,
    const Shape& s,
    bool selected
)
{
    ImU32 color = s.color;

    if (selected)
        color = IM_COL32(255, 210, 40, 255);

    if (s.type == SHAPE_CIRCLE)
    {
        Point center = TransformCircleCenter(s);

        int cx = (int)roundf(center.x);
        int cy = (int)roundf(center.y);

        int radius = (int)roundf(s.radius * s.scale);

        if (radius < 1)
            radius = 1;

        if (s.filled)
        {
            FillCircle(
                draw, origin, cx, cy, radius, s.color
            );
        }

        MidpointCircle(
            draw, origin, cx, cy, radius, color
        );

        return;
    }

    std::vector<Point> points = GetTransformedPoints(s);

    if (s.type == SHAPE_LINE)
    {
        if (points.size() >= 2)
        {
            Bresenham(
                draw, origin,
                (int)roundf(points[0].x),
                (int)roundf(points[0].y),
                (int)roundf(points[1].x),
                (int)roundf(points[1].y),
                color
            );
        }
    }
    else if (s.type == SHAPE_FREEHAND)
    {
        for (size_t i = 1; i < points.size(); i++)
        {
            Bresenham(
                draw, origin,
                (int)roundf(points[i - 1].x),
                (int)roundf(points[i - 1].y),
                (int)roundf(points[i].x),
                (int)roundf(points[i].y),
                color
            );
        }
    }
    else
    {
        DrawPolygon(
            draw,
            origin,
            points,
            color,
            s.filled
        );
    }

   
    if (selected && !points.empty())
    {
        float minX = points[0].x;
        float maxX = points[0].x;
        float minY = points[0].y;
        float maxY = points[0].y;

        for (const Point& p : points)
        {
            minX = std::min(minX, p.x);
            maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y);
            maxY = std::max(maxY, p.y);
        }

        ImVec2 a = GridToScreen({ minX, minY }, origin);
        ImVec2 b = GridToScreen({ maxX + 1, maxY + 1 }, origin);

        draw->AddRect(
            a, b,
            IM_COL32(255, 210, 40, 255),
            0.0f,
            0,
            2.0f
        );
    }
}



bool IsMouseInsideShape(const Shape& s, Point mouse)
{
    if (s.type == SHAPE_CIRCLE)
    {
        Point c = TransformCircleCenter(s);

        float radius = s.radius * s.scale + 1.5f;

        float dx = mouse.x - c.x;
        float dy = mouse.y - c.y;

        return dx * dx + dy * dy <= radius * radius;
    }

    std::vector<Point> points = GetTransformedPoints(s);

    if (points.empty())
        return false;

    float minX = points[0].x;
    float maxX = points[0].x;
    float minY = points[0].y;
    float maxY = points[0].y;

    for (const Point& p : points)
    {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    }

    return mouse.x >= minX - 1.5f &&
        mouse.x <= maxX + 1.5f &&
        mouse.y >= minY - 1.5f &&
        mouse.y <= maxY + 1.5f;
}

int FindShapeAt(Point mouse)
{
    for (int i = (int)shapes.size() - 1; i >= 0; i--)
    {
        if (IsMouseInsideShape(shapes[i], mouse))
            return i;
    }

    return -1;
}



void FinishPendingShape()
{
    if (currentTool == TOOL_LINE && pendingPoints.size() == 2)
    {
        Shape s;
        s.type = SHAPE_LINE;
        s.points = pendingPoints;
        s.color = currentColor;

        shapes.push_back(s);
        pendingPoints.clear();
    }
    else if (currentTool == TOOL_CIRCLE && pendingPoints.size() == 2)
    {
        Shape s;
        s.type = SHAPE_CIRCLE;
        s.points.push_back(pendingPoints[0]);

        float dx = pendingPoints[1].x - pendingPoints[0].x;
        float dy = pendingPoints[1].y - pendingPoints[0].y;

        s.radius = sqrtf(dx * dx + dy * dy);
        s.color = currentColor;
        s.filled = fillEnabled;

        shapes.push_back(s);
        pendingPoints.clear();
    }
    else if (currentTool == TOOL_TRIANGLE && pendingPoints.size() == 3)
    {
        Shape s;
        s.type = SHAPE_TRIANGLE;
        s.points = pendingPoints;
        s.color = currentColor;
        s.filled = fillEnabled;

        shapes.push_back(s);
        pendingPoints.clear();
    }
    else if (currentTool == TOOL_SQUARE && pendingPoints.size() == 2)
    {
        Point a = pendingPoints[0];
        Point b = pendingPoints[1];

        Shape s;
        s.type = SHAPE_SQUARE;

        s.points.push_back({ a.x, a.y });
        s.points.push_back({ b.x, a.y });
        s.points.push_back({ b.x, b.y });
        s.points.push_back({ a.x, b.y });

        s.color = currentColor;
        s.filled = fillEnabled;

        shapes.push_back(s);
        pendingPoints.clear();
    }
}



void DrawEditor()
{
    ImGui::Begin("Editor grafico - Dear ImGui");

    ImGui::Text("Herramientas");

    if (ImGui::Button("Seleccionar"))
        currentTool = TOOL_SELECT;

    ImGui::SameLine();

    if (ImGui::Button("Linea"))
    {
        currentTool = TOOL_LINE;
        pendingPoints.clear();
    }

    ImGui::SameLine();

    if (ImGui::Button("Circulo"))
    {
        currentTool = TOOL_CIRCLE;
        pendingPoints.clear();
    }

    ImGui::SameLine();

    if (ImGui::Button("Forma libre"))
    {
        currentTool = TOOL_FREEHAND;
        pendingPoints.clear();
    }

    if (ImGui::Button("Triangulo"))
    {
        currentTool = TOOL_TRIANGLE;
        pendingPoints.clear();
    }

    ImGui::SameLine();

    if (ImGui::Button("Cuadrado"))
    {
        currentTool = TOOL_SQUARE;
        pendingPoints.clear();
    }

    ImGui::Separator();

    ImGui::Text("Color de dibujo");

    static float color[4] = { 0.86f, 0.20f, 0.20f, 1.0f };

    if (ImGui::ColorEdit4("Color", color))
    {
        currentColor = ImGui::ColorConvertFloat4ToU32(
            ImVec4(color[0], color[1], color[2], color[3])
        );
    }

    ImGui::Checkbox("Rellenar primitivas", &fillEnabled);

    ImGui::SameLine();

    if (ImGui::Button("Limpiar todo"))
    {
        shapes.clear();
        pendingPoints.clear();
        selectedShape = -1;
        isDrawingFreehand = false;
        isDraggingShape = false;
    }

    ImGui::Separator();

    ImGui::Text("Transformaciones del objeto seleccionado");

    if (selectedShape >= 0 &&
        selectedShape < (int)shapes.size())
    {
        Shape& s = shapes[selectedShape];

        ImGui::Text("Objeto seleccionado: %d", selectedShape + 1);

        ImGui::SliderFloat(
            "Rotacion",
            &s.rotation,
            -360.0f,
            360.0f,
            "%.1f grados"
        );

        ImGui::SliderFloat(
            "Escala",
            &s.scale,
            0.1f,
            4.0f,
            "%.2f"
        );

        ImGui::DragFloat("Mover X", &s.offsetX, 0.2f);
        ImGui::DragFloat("Mover Y", &s.offsetY, 0.2f);

        if (ImGui::Button("Restablecer transformaciones"))
        {
            s.rotation = 0;
            s.scale = 1.0f;
            s.offsetX = 0;
            s.offsetY = 0;
        }

        ImGui::SameLine();

        if (ImGui::Button("Eliminar objeto"))
        {
            shapes.erase(shapes.begin() + selectedShape);
            selectedShape = -1;
        }
    }
    else
    {
        ImGui::TextDisabled("Selecciona una figura para transformarla.");
    }

    ImGui::Separator();

    ImGui::Text(
        "Linea, circulo y cuadrado: 2 clics. "
        "Triangulo: 3 clics. Forma libre: mantener clic y dibujar."
    );

    ImGui::Text(
        "Para mover una figura: selecciona y arrastra con el mouse."
    );

    ImGui::Separator();

   
    ImGui::Text("Lienzo / Grilla");

    ImGui::BeginChild(
        "CanvasArea",
        ImVec2(0, 0),
        true,
        ImGuiWindowFlags_NoScrollbar
    );

    ImVec2 canvasSize = ImGui::GetContentRegionAvail();

    if (canvasSize.x < 1) canvasSize.x = 1;
    if (canvasSize.y < 1) canvasSize.y = 1;

    ImVec2 origin = ImGui::GetCursorScreenPos();

    ImDrawList* draw = ImGui::GetWindowDrawList();

    ImVec2 canvasEnd(
        origin.x + canvasSize.x,
        origin.y + canvasSize.y
    );

    draw->AddRectFilled(
        origin,
        canvasEnd,
        IM_COL32(250, 250, 250, 255)
    );

    draw->PushClipRect(origin, canvasEnd, true);

    
    for (float x = origin.x; x < canvasEnd.x; x += CELL_SIZE)
    {
        draw->AddLine(
            ImVec2(x, origin.y),
            ImVec2(x, canvasEnd.y),
            IM_COL32(220, 220, 220, 255)
        );
    }

    for (float y = origin.y; y < canvasEnd.y; y += CELL_SIZE)
    {
        draw->AddLine(
            ImVec2(origin.x, y),
            ImVec2(canvasEnd.x, y),
            IM_COL32(220, 220, 220, 255)
        );
    }

    
    for (int i = 0; i < (int)shapes.size(); i++)
    {
        DrawShape(
            draw,
            origin,
            shapes[i],
            i == selectedShape
        );
    }

    
    for (const Point& p : pendingPoints)
    {
        ImVec2 center = GridCenterToScreen(p, origin);

        draw->AddCircleFilled(
            center,
            5,
            IM_COL32(20, 120, 240, 255)
        );
    }

    
    ImGui::Dummy(canvasSize);

    bool hovered = ImGui::IsItemHovered();
    ImVec2 mouse = ImGui::GetIO().MousePos;

    if (hovered)
    {
        Point gridMouse = MouseToGrid(mouse, origin);

        
        if (currentTool == TOOL_SELECT)
        {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                selectedShape = FindShapeAt(gridMouse);

                if (selectedShape >= 0)
                {
                    isDraggingShape = true;
                    lastMousePosition = mouse;
                }
            }

            if (isDraggingShape &&
                ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
                selectedShape >= 0 &&
                selectedShape < (int)shapes.size())
            {
                ImVec2 delta(
                    mouse.x - lastMousePosition.x,
                    mouse.y - lastMousePosition.y
                );

                shapes[selectedShape].offsetX += delta.x / CELL_SIZE;
                shapes[selectedShape].offsetY += delta.y / CELL_SIZE;

                lastMousePosition = mouse;
            }
        }

        
        else if (currentTool == TOOL_FREEHAND)
        {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                Shape s;
                s.type = SHAPE_FREEHAND;
                s.color = currentColor;
                s.points.push_back(gridMouse);

                shapes.push_back(s);

                selectedShape = (int)shapes.size() - 1;
                isDrawingFreehand = true;
            }

            if (isDrawingFreehand &&
                ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                if (selectedShape >= 0 &&
                    selectedShape < (int)shapes.size())
                {
                    Shape& s = shapes[selectedShape];

                    if (s.points.empty() ||
                        s.points.back().x != gridMouse.x ||
                        s.points.back().y != gridMouse.y)
                    {
                        s.points.push_back(gridMouse);
                    }
                }
            }
        }

        
        else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            pendingPoints.push_back(gridMouse);
            FinishPendingShape();
        }
    }

    
    if (isDrawingFreehand &&
        ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        isDrawingFreehand = false;
    }

    
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        isDraggingShape = false;
    }

    draw->PopClipRect();

    ImGui::EndChild();
    ImGui::End();
}



LRESULT WINAPI WndProc(
    HWND hWnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam
)
{
    if (ImGui_ImplWin32_WndProcHandler(
        hWnd, msg, wParam, lParam))
    {
        return true;
    }

    switch (msg)
    {
    case WM_SIZE:
        if (g_pd3dDevice != nullptr &&
            wParam != SIZE_MINIMIZED)
        {
            CleanupRenderTarget();

            g_pSwapChain->ResizeBuffers(
                0,
                (UINT)LOWORD(lParam),
                (UINT)HIWORD(lParam),
                DXGI_FORMAT_UNKNOWN,
                0
            );

            CreateRenderTarget();
        }
        return 0;

    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}



int WINAPI WinMain(
    HINSTANCE hInstance,
    HINSTANCE,
    LPSTR,
    int
)
{
    WNDCLASSEX wc =
    {
        sizeof(WNDCLASSEX),
        CS_CLASSDC,
        WndProc,
        0L,
        0L,
        GetModuleHandle(nullptr),
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        _T("EditorGrafico"),
        nullptr
    };

    RegisterClassEx(&wc);

    HWND hwnd = CreateWindow(
        wc.lpszClassName,
        _T("Editor grafico - C++ Dear ImGui"),
        WS_OVERLAPPEDWINDOW,
        100,
        100,
        1200,
        850,
        nullptr,
        nullptr,
        wc.hInstance,
        nullptr
    );

    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        UnregisterClass(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();

    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    (void)io;

    ImGui::StyleColorsLight();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(
        g_pd3dDevice,
        g_pd3dDeviceContext
    );

    bool done = false;

    while (!done)
    {
        MSG msg;

        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);

            if (msg.message == WM_QUIT)
                done = true;
        }

        if (done)
            break;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        DrawEditor();

        ImGui::Render();

        const float clearColor[4] =
        {
            0.90f,
            0.90f,
            0.90f,
            1.00f
        };

        g_pd3dDeviceContext->OMSetRenderTargets(
            1,
            &g_mainRenderTargetView,
            nullptr
        );

        g_pd3dDeviceContext->ClearRenderTargetView(
            g_mainRenderTargetView,
            clearColor
        );

        ImGui_ImplDX11_RenderDrawData(
            ImGui::GetDrawData()
        );

        g_pSwapChain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();

    ImGui::DestroyContext();

    CleanupDeviceD3D();

    DestroyWindow(hwnd);
    UnregisterClass(wc.lpszClassName, wc.hInstance);

    return 0;
}
