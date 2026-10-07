#pragma once

#include "CoreMinimal.h"

class UWidgetBlueprint;

namespace ShopUILayout
{
    // Keep editable named captions and bindings; fit text inside existing button geometry.
    bool FitText(UWidgetBlueprint* Blueprint);
    // Preserve the supplied psychic artwork and center its value inside the original number field.
    bool RestorePsychicArtwork(UWidgetBlueprint* Blueprint);
    // Align the other HUD values on one centerline while retaining the original psychic artwork.
    bool AlignHud(UWidgetBlueprint* Blueprint);
    // Update only the root widget's psychic artwork and numeric placement.
    bool RestorePsychicProject();
    // Explicit in-place upgrade; never regenerate the user's pages, graphs or artwork.
    bool RefineProject();
}
