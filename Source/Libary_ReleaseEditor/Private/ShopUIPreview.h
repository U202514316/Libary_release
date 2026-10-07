#pragma once

#include "CoreMinimal.h"

class UUserWidget;

namespace ShopUIPreview
{
    /** Render the supplied, already-created Widget Blueprint (default 1920x1080).
        Writes Saved/UIBuild/Preview/Name.png without changing its page or properties.
        Returns true when saved OR explicitly skipped because rendering is unavailable;
        the log distinguishes those outcomes. Run with -AllowCommandletRendering for PNG output. */
    bool Save(UUserWidget* Root, const FString& Name, FIntPoint Resolution = FIntPoint(1920, 1080));
}
