#include "ERPlayerController.h"
#include "ERHUDLayout.h"
#include "ERCharacter.h"
#include "ERMovement.h"
#include "Engine/Canvas.h"
#include "Engine/Texture2D.h"
#include "CanvasItem.h"
#include "UObject/ConstructorHelpers.h"

AERHUD::AERHUD()
{
    ConstructorHelpers::FObjectFinder<UTexture2D> DroneTexture(TEXT("/Game/EclipseRecoil/Art/HUDRecon/T_drone.T_drone"));
    DroneIcon=DroneTexture.Object;
    const TCHAR* Names[]={TEXT("01_knife"),TEXT("02_pistol"),TEXT("03_smg_mp9"),TEXT("04_shotgun_xm1014"),TEXT("05_assault_rifle"),TEXT("06_carbine_marksman"),TEXT("07_sniper_rifle"),TEXT("08_grenade_launcher_m32"),TEXT("09_he_grenade"),TEXT("10_smoke_grenade"),TEXT("11_claymore")};
    for(const TCHAR* Name:Names)
    {
        const FString Path=FString::Printf(TEXT("/Game/EclipseRecoil/Art/HUDWeapons/T_%s.T_%s"),Name,Name);
        ConstructorHelpers::FObjectFinder<UTexture2D> Texture(*Path);
        WeaponIcons.Add(Texture.Object);
    }
    // Hard references keep every overlay in cooked builds as well as the editor.
    for (const TCHAR* Kind : {TEXT("health"),TEXT("armor"),TEXT("stamina")})
        for (int32 Layer=0;Layer<17;++Layer)
        {
            const FString Name=FString::Printf(TEXT("T_%s_%s"),Kind,
                Layer==0 ? TEXT("full") : *FString::Printf(TEXT("black_sector_%02d"),Layer-1));
            ConstructorHelpers::FObjectFinder<UTexture2D> Texture(
                *(TEXT("/Game/EclipseRecoil/Art/HUDVitals/")+Name+TEXT(".")+Name));
            VitalsTextures.Add(Texture.Object);
        }
}

void AERHUD::DrawVitalIcon(int32 Icon, FVector2D TopLeft, float Diameter, float Fraction)
{
    auto DrawLayer=[&](int32 Layer)
    {
        const int32 Index=Icon*17+Layer;
        if(!VitalsTextures.IsValidIndex(Index) || !VitalsTextures[Index]) return;
        FCanvasTileItem Tile(TopLeft,VitalsTextures[Index]->GetResource(),FVector2D(Diameter,Diameter),FLinearColor::White);
        Tile.BlendMode=SE_BLEND_Translucent;
        Canvas->DrawItem(Tile);
    };
    DrawLayer(0);
    // Authored sectors start clockwise from twelve; darken N..15 at 1/16 steps.
    const int32 Filled=FMath::RoundToInt32(FMath::Clamp(Fraction,0.f,1.f)*16.f);
    for(int32 Sector=Filled;Sector<16;++Sector) DrawLayer(Sector+1);
}

void AERHUD::DrawVitals(const AERCharacter* Character)
{
    const auto* Movement=Cast<UERMovement>(Character->GetCharacterMovement());
    const FERHUDLayout Layout(FVector2D(Canvas->SizeX,Canvas->SizeY));
    const float Scale=Layout.Scale;
    const FVector2D Edge=Layout.Edge();
    // 206 x 66 reference units: same footprint as the previous three bar rows.
    const float Diameter=66.f*Scale, Step=70.f*Scale;
    const float Top=Canvas->SizeY-Edge.Y-Diameter;
    const float Fractions[]={Character->Health->Health/ERServer::Get(GetWorld(),"Health.Max"),Character->Health->Armor/FMath::Max(1.f,ERServer::Get(GetWorld(),"Health.Armor")),Movement->GetStaminaFraction()};
    for(int32 Icon=0;Icon<3;++Icon) DrawVitalIcon(Icon,FVector2D(Edge.X+Icon*Step,Top),Diameter,Fractions[Icon]);
    if(Movement->bProneWanted || Movement->IsProne())
        DrawHUDTextAnchored(TEXT("PRONE  |  C / Ctrl to stand up"),FVector2D(Edge.X,Top-6.f*Scale),FLinearColor::White,.85f,FVector2D(0,1));
}
