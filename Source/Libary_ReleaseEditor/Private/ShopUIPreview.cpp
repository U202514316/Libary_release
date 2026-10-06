#include "ShopUIPreview.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "DynamicRHI.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Interfaces/ISlateRHIRendererModule.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "Slate/WidgetRenderer.h"
#include "TextureResource.h"
#include "TextureCompiler.h"
#include "UObject/StrongObjectPtr.h"

DEFINE_LOG_CATEGORY_STATIC(LogShopUIPreview, Log, All);

bool ShopUIPreview::Save(UUserWidget* Root, const FString& Name)
{
    static bool bLoggedContext = false;
    if (!bLoggedContext)
    {
        UE_LOG(LogShopUIPreview, Display, TEXT("Rendering context: CanEverRender=%d NullRHI=%d DynamicRHI=%d SlateInitialized=%d Commandlet=%d AllowCommandletRendering=%d GameThread=%d"),
            FApp::CanEverRender(), GUsingNullRHI, GDynamicRHI != nullptr, FSlateApplication::IsInitialized(),
            IsRunningCommandlet(), IsAllowCommandletRendering(), IsInGameThread());
        bLoggedContext = true;
    }
    if (!FApp::CanEverRender() || GUsingNullRHI || GDynamicRHI == nullptr)
    {
        UE_LOG(LogShopUIPreview, Display, TEXT("SKIPPED preview '%s': GPU rendering is unavailable. Use -AllowCommandletRendering without -NullRHI."), *Name);
        return true;
    }
    if (!IsInGameThread() || !IsValid(Root) || Root->GetRootWidget() == nullptr)
    {
        UE_LOG(LogShopUIPreview, Warning, TEXT("Cannot render preview '%s': expected an initialized widget on the game thread."), *Name);
        return false;
    }
    if (!FSlateApplication::IsInitialized())
    {
        if (!IsRunningCommandlet() || !IsAllowCommandletRendering())
        {
            UE_LOG(LogShopUIPreview, Display, TEXT("SKIPPED preview '%s': Slate is unavailable outside an opted-in rendering commandlet."), *Name);
            return true;
        }
        ISlateRHIRendererModule* SlateModule = FModuleManager::Get().LoadModulePtr<ISlateRHIRendererModule>(TEXT("SlateRHIRenderer"));
        if (!SlateModule)
        {
            UE_LOG(LogShopUIPreview, Warning, TEXT("Cannot render preview '%s': SlateRHIRenderer module failed to load."), *Name);
            return false;
        }
        // Rendering commandlets initialize the RHI but normally omit the Slate application.
        // Standalone initialization adds no native window; DrawWidget uses an SVirtualWindow.
        // Engine shutdown owns the application's lifetime after this one-time initialization.
        FSlateApplication::InitializeAsStandaloneApplication(SlateModule->CreateSlateRHIRenderer());
        UE_LOG(LogShopUIPreview, Display, TEXT("Initialized standalone Slate for offscreen Widget Blueprint previews; no native window was created."));
    }
    if (FSlateApplication::Get().GetRenderer() == nullptr)
    {
        UE_LOG(LogShopUIPreview, Warning, TEXT("Cannot render preview '%s': Slate renderer is unavailable after initialization."), *Name);
        return false;
    }

    // Restrict names to a filename beneath the preview directory, including for callers
    // that supply a phase label containing filesystem separators.
    const FString SafeName = FPaths::MakeValidFileName(Name, TEXT('_'));
    if (SafeName.IsEmpty() || SafeName == TEXT(".") || SafeName == TEXT(".."))
    {
        UE_LOG(LogShopUIPreview, Warning, TEXT("Cannot render preview: filename is empty or invalid."));
        return false;
    }
    const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UIBuild/Preview"));
    if (!IFileManager::Get().MakeDirectory(*Directory, true))
    {
        UE_LOG(LogShopUIPreview, Warning, TEXT("Cannot create preview directory: %s"), *Directory);
        return false;
    }

    constexpr int32 Width = 1920;
    constexpr int32 Height = 1080;
    // A commandlet does not tick the editor's asynchronous texture compiler or
    // streamer between page changes. Wait only for images referenced by this tree,
    // otherwise Slate can capture the checkerboard placeholder instead of the art.
    TArray<UTexture*> ImageTextures;
    const auto AddBrushTexture = [&ImageTextures](const FSlateBrush& Brush)
    {
        if (UTexture2D* Texture = Cast<UTexture2D>(Brush.GetResourceObject()))
            ImageTextures.AddUnique(Texture);
    };
    Root->WidgetTree->ForEachWidgetAndDescendants([&AddBrushTexture](UWidget* Widget)
    {
        if (const UImage* Image = Cast<UImage>(Widget))
        {
            AddBrushTexture(Image->Brush);
            // Bound cover brushes are evaluated by Slate during paint, and may
            // reference a different texture than the Designer's stored brush.
            if (Image->BrushDelegate.IsBound()) AddBrushTexture(Image->BrushDelegate.Execute());
        }
        if (const UBorder* Border = Cast<UBorder>(Widget))
        {
            AddBrushTexture(Border->Background);
            if (Border->BackgroundDelegate.IsBound()) AddBrushTexture(Border->BackgroundDelegate.Execute());
        }
    });
    FTextureCompilingManager::Get().FinishCompilation(ImageTextures);
    for (UTexture* ImageTexture : ImageTextures)
    {
        UTexture2D* Texture = CastChecked<UTexture2D>(ImageTexture);
        Texture->WaitForPendingInitOrStreaming();
        Texture->SetForceMipLevelsToBeResident(1.0f);
        Texture->WaitForStreaming();
    }
    FlushRenderingCommands();
    FWidgetRenderer Renderer(true, true);
    if (Renderer.GetSlateRenderer() == nullptr)
    {
        UE_LOG(LogShopUIPreview, Display, TEXT("SKIPPED preview '%s': offscreen Slate renderer is unavailable."), *Name);
        return true;
    }
    // Slate already emits display-encoded colors when gamma correction is enabled.
    // UE5.1's convenience DrawWidget overload creates an sRGB render target too,
    // applying a second conversion on GPU writes. Use an explicit non-sRGB target
    // for PNG readback, preserving the Widget Blueprint's actual colors.
    TStrongObjectPtr<UTextureRenderTarget2D> Target(NewObject<UTextureRenderTarget2D>());
    Target->ClearColor = FLinearColor::Transparent;
    Target->TargetGamma = 1.0f;
    Target->InitCustomFormat(Width, Height, PF_B8G8R8A8, true);
    Target->UpdateResourceImmediate(true);
    Renderer.DrawWidget(Target.Get(), Root->TakeWidget(), FVector2D(Width, Height), 0.0f, false);
    FlushRenderingCommands();
    FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
    TArray<FColor> Pixels;
    FReadSurfaceDataFlags Flags(RCM_UNorm);
    Flags.SetLinearToGamma(false);
    if (Resource == nullptr || !Resource->ReadPixels(Pixels, Flags) || Pixels.Num() != Width * Height)
    {
        UE_LOG(LogShopUIPreview, Warning, TEXT("Cannot render preview '%s': pixel readback failed."), *Name);
        return false;
    }

    TArray64<uint8> Png;
    // CompressImageArray is deprecated in 5.1 and may choose a thumbnail JPEG.
    // Explicit PNG encoding keeps the file contents consistent with the .png extension.
    FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
    const FString Filename = FPaths::Combine(Directory, SafeName + TEXT(".png"));
    if (Png.IsEmpty() || !FFileHelper::SaveArrayToFile(Png, *Filename))
    {
        UE_LOG(LogShopUIPreview, Warning, TEXT("Cannot save preview '%s': PNG compression or file writing failed."), *Filename);
        return false;
    }
    UE_LOG(LogShopUIPreview, Display, TEXT("SAVED preview %s (%dx%d)"), *FPaths::ConvertRelativePathToFull(Filename), Width, Height);
    return true;
}
