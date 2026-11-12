#include "ERControls.h"
#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
    constexpr TCHAR Section[] = TEXT("EclipseRecoil.Controls");

    bool CanBind(const FKey& DefaultKey, const FKey& Key)
    {
        if (!Key.IsValid() || Key == EKeys::AnyKey || Key.IsAnalog() || Key.IsGamepadKey() || Key.IsTouch())
            return false;
        if (Key == EKeys::MouseScrollUp || Key == EKeys::MouseScrollDown)
            return DefaultKey == EKeys::MouseScrollUp || DefaultKey == EKeys::MouseScrollDown;
        return true;
    }
}

FKey FERControls::SlotKey(int32 Slot)
{
    static const FKey Keys[] = {EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five,
        EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::F, EKeys::G, EKeys::H};
    return Slot >= 0 && Slot < UE_ARRAY_COUNT(Keys) ? Keys[Slot] : EKeys::Invalid;
}

TConstArrayView<FERControlBinding> FERControls::Bindings()
{
    static const FERControlBinding Items[] = {
        {EKeys::W, TEXT("Move forward / ladder up")},
        {EKeys::S, TEXT("Move back / ladder down")},
        {EKeys::A, TEXT("Move left")}, {EKeys::D, TEXT("Move right")},
        {EKeys::LeftShift, TEXT("Walk")}, {EKeys::RightShift, TEXT("Walk (alternate)")},
        {EKeys::E, TEXT("Sprint (hold with forward)")}, {EKeys::SpaceBar, TEXT("Jump / climb / respawn")},
        {EKeys::C, TEXT("Crouch")}, {EKeys::LeftControl, TEXT("Crouch (alternate)")},
        {EKeys::LeftMouseButton, TEXT("Fire / use / respawn")},
        {EKeys::RightMouseButton, TEXT("Aim / zoom")}, {EKeys::R, TEXT("Reload")},
        {EKeys::MouseScrollUp, TEXT("Next item")}, {EKeys::MouseScrollDown, TEXT("Previous item")},
        {SlotKey(0), TEXT("Item shortcut 1")}, {SlotKey(1), TEXT("Item shortcut 2")},
        {SlotKey(2), TEXT("Item shortcut 3")}, {SlotKey(3), TEXT("Item shortcut 4")},
        {SlotKey(4), TEXT("Item shortcut 5")}, {SlotKey(5), TEXT("Item shortcut 6")},
        {SlotKey(6), TEXT("Item shortcut 7")}, {SlotKey(7), TEXT("Item shortcut 8")},
        {SlotKey(8), TEXT("HE grenade")}, {SlotKey(9), TEXT("Smoke grenade")}, {SlotKey(10), TEXT("Claymore")},
        {EKeys::Tab, TEXT("Scoreboard")}, {EKeys::Escape, TEXT("Menu")}, {EKeys::P, TEXT("Screenshots")},
    };
    return MakeArrayView(Items);
}

FString FERControls::ShortLabel(const FKey& Key)
{
    if (Key == EKeys::LeftMouseButton) return TEXT("LMB");
    if (Key == EKeys::RightMouseButton) return TEXT("RMB");
    if (Key == EKeys::MiddleMouseButton) return TEXT("MMB");
    if (Key == EKeys::ThumbMouseButton) return TEXT("MB4");
    if (Key == EKeys::ThumbMouseButton2) return TEXT("MB5");
    if (Key == EKeys::MouseScrollUp) return TEXT("WHEEL UP");
    if (Key == EKeys::MouseScrollDown) return TEXT("WHEEL DOWN");
    if (Key == EKeys::Escape) return TEXT("ESC");
    if (Key == EKeys::SpaceBar) return TEXT("SPACE");
    if (Key == EKeys::LeftShift) return TEXT("L SHIFT");
    if (Key == EKeys::RightShift) return TEXT("R SHIFT");
    if (Key == EKeys::LeftControl) return TEXT("L CTRL");
    if (Key == EKeys::RightControl) return TEXT("R CTRL");
    return Key.GetDisplayName(false).ToString().ToUpper();
}

FKey FERControls::Key(const FKey& DefaultKey) const
{
    if (const FKey* Saved = Overrides.Find(DefaultKey)) return *Saved;
    return DefaultKey;
}

bool FERControls::Rebind(const FKey& DefaultKey, const FKey& NewKey)
{
    if (!Bindings().ContainsByPredicate([&](const FERControlBinding& Item) { return Item.DefaultKey == DefaultKey; })
        || !CanBind(DefaultKey, NewKey)) return false;

    const FKey OldKey = Key(DefaultKey);
    if (OldKey == NewKey) return true;
    for (const FERControlBinding& Item : Bindings())
    {
        if (Key(Item.DefaultKey) != NewKey) continue;
        if (!CanBind(Item.DefaultKey, OldKey)) return false;
        Overrides.Add(Item.DefaultKey, OldKey);
        break;
    }
    Overrides.Add(DefaultKey, NewKey);
    Save();
    return true;
}

void FERControls::Load()
{
    Overrides.Reset();
    TSet<FKey> Used;
    for (const FERControlBinding& Item : Bindings())
    {
        FString Saved;
        FKey ActiveKey = Item.DefaultKey;
        if (GConfig->GetString(Section, *Item.DefaultKey.ToString(), Saved, GGameUserSettingsIni))
        {
            const FKey SavedKey{FName(*Saved)};
            if (CanBind(Item.DefaultKey, SavedKey)) ActiveKey = SavedKey;
        }
        if (Used.Contains(ActiveKey))
        {
            Overrides.Reset();
            return;
        }
        Used.Add(ActiveKey);
        if (ActiveKey != Item.DefaultKey) Overrides.Add(Item.DefaultKey, ActiveKey);
    }
}

void FERControls::Save() const
{
    // Unreal clears a user settings file without its engine version on startup.
    int32 Version = 0;
    if (!GConfig->GetInt(TEXT("/Script/Engine.GameUserSettings"), TEXT("Version"), Version, GGameUserSettingsIni))
        if (auto* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr)
            Settings->SaveSettings();

    for (const FERControlBinding& Item : Bindings())
    {
        const FString ConfigKey = Item.DefaultKey.ToString();
        const FKey ActiveKey = Key(Item.DefaultKey);
        if (ActiveKey == Item.DefaultKey) GConfig->RemoveKey(Section, *ConfigKey, GGameUserSettingsIni);
        else GConfig->SetString(Section, *ConfigKey, *ActiveKey.ToString(), GGameUserSettingsIni);
    }
    GConfig->Flush(false, GGameUserSettingsIni);
}

void FERControls::Reset()
{
    Overrides.Reset();
    Save();
}
