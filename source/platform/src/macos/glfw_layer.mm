#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>

namespace vultra::platform
{
    void* glfwMetalLayer(void* nativeWindow)
    {
        NSWindow* window = (__bridge NSWindow*)nativeWindow;
        NSView* view = window.contentView;
        if (!view)
        {
            return nullptr;
        }
        if (![view.layer isKindOfClass:[CAMetalLayer class]])
        {
            view.wantsLayer = YES;
            view.layer = [CAMetalLayer layer];
        }
        return (__bridge void*)view.layer;
    }
} // namespace vultra::platform
