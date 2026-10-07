#pragma once
class UWidgetBlueprint;
namespace ShopAudioAuthoring
{
    // Adds named, idempotent execution hooks; Designer layouts are never rebuilt.
    bool WireWidget(UWidgetBlueprint* Blueprint);
}
