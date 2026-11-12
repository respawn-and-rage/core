#include "ERPlayerController.h"
#include "ERCharacter.h"
#include "ERWeapon.h"
#include "ERLoadout.h"
#include "ERHUDLayout.h"
#include "ERRecon.h"
#include "Engine/Canvas.h"
#include "Engine/Texture2D.h"
#include "CanvasItem.h"

namespace
{
    // Crop transparent margins through UVs only; source images remain untouched.
    const FIntRect IconBounds[]={ {131,159,893,353},{216,86,808,425},{121,93,903,418},
        {58,152,965,359},{61,108,963,403},{66,116,957,396},{41,132,983,379},
        {76,121,947,391},{357,72,667,439},{420,72,603,439},{436,100,1338,806} };
    // Similar apparent grip size across firearms, rather than equal overall widths.
    const float IconScales[]={.11f,.14f,.145f,.17f,.16f,.16f,.19f,.16f,.095f,.095f,.05f};
}

float AERHUD::WeaponIconWidth(int32 Key, float Height) const
{
    if(Key<0 || Key>=int32(UE_ARRAY_COUNT(IconBounds)) || !WeaponIcons.IsValidIndex(Key) || !WeaponIcons[Key])return 0;
    const FIntRect B=IconBounds[Key];
    // Uniform kill feed height, bounded so long rifles do not dominate the row.
    return FMath::Min(Height*B.Width()/float(FMath::Max(1,B.Height())),Height*4.2f);
}

float AERHUD::DrawWeaponIcon(int32 Key, FVector2D Position, float Height, FLinearColor Color)
{
    const float Width=WeaponIconWidth(Key,Height);
    if(!Canvas || Width<=0)return 0;
    const auto* Texture=WeaponIcons[Key].Get();
    const FIntRect B=IconBounds[Key];
    const float DrawHeight=Width*B.Height()/float(FMath::Max(1,B.Width()));
    FCanvasTileItem Tile(FVector2D(Position.X,Position.Y-DrawHeight*.5f),Texture->GetResource(),FVector2D(Width,DrawHeight),
        FVector2D(float(B.Min.X)/Texture->GetSizeX(),float(B.Min.Y)/Texture->GetSizeY()),
        FVector2D(float(B.Max.X)/Texture->GetSizeX(),float(B.Max.Y)/Texture->GetSizeY()),Color);
    Tile.BlendMode=SE_BLEND_Translucent;Canvas->DrawItem(Tile);
    return Width;
}

void AERHUD::DrawWeaponInventory(const AERCharacter* Character)
{
    const FERHUDLayout Layout(FVector2D(Canvas->SizeX,Canvas->SizeY));
    const float S=Layout.Scale;
    const FVector2D Edge=Layout.Edge();
    const float Right=Canvas->SizeX-Edge.X, Bottom=Canvas->SizeY-Edge.Y;
    const auto& Bounds=IconBounds;
    const auto* PlayerState=Character->GetPlayerState<AERPlayerState>();
    const auto* PC = Cast<AERPlayerController>(PlayerOwner);
    const FLinearColor TeamColor=PlayerState?ERRecon::TeamColor(PlayerState->Team):FLinearColor::White;
    int32 Row=0;
    for(int32 Slot=0;Slot<Character->Inventory.Num();++Slot)
    {
        const auto* Item=Character->Inventory[Slot].Get();
        if(!Item || !Item->Data)continue;
        int32 Key=INDEX_NONE;
        for(int32 I=0;I<11;++I)if(Item->Data->GetFName()==ERLoadouts::WeaponAssetName(I)){Key=I;break;}
        if(Key==INDEX_NONE || !WeaponIcons.IsValidIndex(Key) || !WeaponIcons[Key])continue;
        const auto* Texture=WeaponIcons[Key].Get();
        const FIntRect B=Bounds[Key];
        const FVector2D Size(B.Width()*IconScales[Key]*.9f*S,B.Height()*IconScales[Key]*.9f*S);
        const float CenterY=Bottom-(64.f+Row*54.f)*S;
        const bool Selected=Item==Character->Weapon.Get();
        const bool Empty=Item->Data->Payload!=EERPayload::Melee && Item->MagazineAmmo+Item->ReserveAmmo<=0;
        const float Opacity=Empty?.33f:.9f;
        FLinearColor IconColor=Selected?TeamColor:FLinearColor::White;
        IconColor.A=Opacity;
        // Presets bind stable catalog keys, independent of inventory ordering.
        const FKey DefaultKey = FERControls::SlotKey(Character->UsesReferenceArsenal() ? Key : Slot);
        const FString Input = FERControls::ShortLabel(PC ? PC->ControlKey(DefaultKey) : DefaultKey);
        const float KeyColumnWidth = FMath::Max(28.0 * S, MeasureHUDText(Input, .5f).X + 8.0 * S);
        FCanvasTileItem Tile(FVector2D(Right-KeyColumnWidth-Size.X,CenterY-Size.Y*.5f),Texture->GetResource(),Size,
            FVector2D(float(B.Min.X)/Texture->GetSizeX(),float(B.Min.Y)/Texture->GetSizeY()),
            FVector2D(float(B.Max.X)/Texture->GetSizeX(),float(B.Max.Y)/Texture->GetSizeY()),IconColor);
        Tile.BlendMode=SE_BLEND_Translucent;Canvas->DrawItem(Tile);
        // Center the visible Teko capitals/digits, excluding the line box descender space.
        DrawHUDTextAnchored(Input,FVector2D(Right,CenterY),FLinearColor(1,1,1,Opacity),.5f,FVector2D(1,.447f));
        ++Row;
    }
    if(const auto* Selected=Character->Weapon.Get();Selected && Selected->Data && Selected->Data->Payload!=EERPayload::Melee)
    {
        const int32 Loaded=FMath::Max(0,Selected->MagazineAmmo),Total=Loaded+FMath::Max(0,Selected->ReserveAmmo);
        const bool CountOnly=AERWeapon::IsHandGrenade(Selected->Data->Payload) || Selected->Data->Payload==EERPayload::Claymore;
        const FString Ammo=CountOnly?FString::FromInt(Total):FString::Printf(TEXT("%d / %d"),Loaded,Total);
        DrawHUDTextAnchored(Ammo,FVector2D(Right,Bottom),Selected->bReloading?FLinearColor(1,.7f,.25f):FLinearColor::White,1.1f,FVector2D(1,1));
    }
}
