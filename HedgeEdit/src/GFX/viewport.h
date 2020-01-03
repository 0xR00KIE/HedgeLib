#pragma once
#ifdef D3D11
#include <winrt/base.h>
#include <d3d11.h>
#include <dxgi.h>
#include <DirectXMath.h>
#endif

#include <array>

namespace HedgeEdit::GFX
{
    class Instance;

#ifdef _WIN32
    using WindowHandle = HWND;
#endif

    class Viewport
    {
        Instance& inst;
        WindowHandle handle;

#ifdef D3D11
        winrt::com_ptr<IDXGISwapChain> swapChain;
        winrt::com_ptr<ID3D11RenderTargetView> renderTargetView;
        winrt::com_ptr<ID3D11Texture2D> depthBuffer;
        winrt::com_ptr<ID3D11DepthStencilView> depthView;
        winrt::com_ptr<ID3D11DepthStencilState> depthState;

        DirectX::XMVECTOR camPos, camRot, camUp, camForward;
        DirectX::XMMATRIX view, proj, viewProj;
#endif

        void Init(unsigned int width, unsigned int height);

        void UpdateCameraForward();

        inline void UpdateViewMatrix()
        {
#ifdef D3D11
            view = DirectX::XMMatrixLookAtRH(camPos,
                DirectX::XMVectorAdd(camPos, camForward),
                camUp);
#endif
        }

        inline void UpdateViewProjMatrix()
        {
#ifdef D3D11
            viewProj = DirectX::XMMatrixMultiply(view, proj);
#endif
        }

    public:
        std::array<float, 4> ClearColor{ 0, 0, 0, 1 };
        float CameraSensitivity = 0.25f, SlowSpeed = 0.25f,
            NormalSpeed = 0.5f, FastSpeed = 1;

        bool MovingForward = false, MovingBackward = false,
            MovingLeft = false, MovingRight = false, Moving = false;

        Viewport(Instance& inst, WindowHandle handle,
            unsigned int width, unsigned int height);

        inline void ChangeHandle(WindowHandle handle)
        {
            if (handle) this->handle = handle;
        }

        void RotateCamera(int amountX, int amountY);
        void Resize(unsigned int width, unsigned int height);
        void Update();
        void Render();
    };
}
