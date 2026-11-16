#include "ERPlayerController.h"
#include "ERCharacter.h"
#include "ERHUDLayout.h"
#include "Engine/Canvas.h"
#include "CanvasItem.h"
#include "Engine/Texture.h"

void AERPlayerController::ClientInflictedDamage_Implementation(float HealthDamage,float ArmorDamage)
{
    if(!IsLocalController() || (HealthDamage<=0 && ArmorDamage<=0))return;
    const double Now=GetWorld()->GetTimeSeconds();
    DamageNotices.RemoveAll([Now](const FDamageNotice& N){return Now-N.Time>=1.2;});
    DamageNotices.Insert({FMath::Max(0.f,HealthDamage),FMath::Max(0.f,ArmorDamage),Now},0);
    if(DamageNotices.Num()>5)DamageNotices.SetNum(5);
#if !UE_BUILD_SHIPPING
    if(FParse::Param(FCommandLine::Get(),TEXT("ERAimPunchAudit")))
        UE_LOG(LogTemp,Display,TEXT("ER_DAMAGE_NOTICE health=%.2f armor=%.2f visible=%d"),HealthDamage,ArmorDamage,DamageNotices.Num());
#endif
}

void AERHUD::DrawInflictedDamage()
{
    auto* PC=Cast<AERPlayerController>(PlayerOwner);
    if(!PC)return;
    const double Now=GetWorld()->GetTimeSeconds();
    PC->DamageNotices.RemoveAll([Now](const AERPlayerController::FDamageNotice& N){return Now-N.Time>=1.2;});
    const float Scale=FERHUDLayout(FVector2D(Canvas->SizeX,Canvas->SizeY)).Scale;
    // Match the bright red/green fills of the authored circular vitals textures.
    const FLinearColor HealthColor=FLinearColor::FromSRGBColor(FColor(251,64,64));
    const FLinearColor ArmorColor=FLinearColor::FromSRGBColor(FColor(29,205,97));
    for(int32 I=0;I<PC->DamageNotices.Num();++I)
    {
        const auto& N=PC->DamageNotices[I];
        const float Alpha=FMath::Clamp(1.f-float(Now-N.Time-1.0)/.2f,0.f,1.f);
        const float X=Canvas->SizeX*.5f,Y=Canvas->SizeY*.5f+(64.f+I*17.f)*Scale;
        DrawHUDTextAnchored(FString::Printf(TEXT("-%.0f HP"),N.Health),FVector2D(X-5.f*Scale,Y),
            HealthColor.CopyWithNewOpacity(Alpha),.5f,FVector2D(1,0));
        DrawHUDTextAnchored(FString::Printf(TEXT("-%.0f AP"),N.Armor),FVector2D(X+5.f*Scale,Y),
            ArmorColor.CopyWithNewOpacity(Alpha),.5f,FVector2D(0,0));
    }
}

void AERHUD::DrawDamageFeedback(const AERCharacter* Character)
{
    if (!Character || PlayerOwner->GetViewTarget() != Character) return;
    const bool bDead = Character->Health->bDead;
    const auto& Events=Character->GetDamageVisuals();
    if(Events.IsEmpty())return;
    const float W=Canvas->SizeX,H=Canvas->SizeY,Scale=FERHUDLayout(FVector2D(W,H)).Scale;
    const FVector2D Center(W*.5f,H*.5f);
    const double Now=GetWorld()->GetTimeSeconds();
    TArray<FCanvasUVTri> Triangles;
    auto Tri=[&](FVector2D A,FVector2D B,FVector2D C,FLinearColor CA,FLinearColor CB,FLinearColor CC)
    {
        FCanvasUVTri T;
        T.V0_Pos=A;T.V1_Pos=B;T.V2_Pos=C;
        T.V0_Color=CA;T.V1_Color=CB;T.V2_Color=CC;
        T.V0_UV=T.V1_UV=T.V2_UV=FVector2D::ZeroVector;
        Triangles.Add(T);
    };
    float Flash=0;
    for(const auto& E:Events)
    {
        if (bDead && !E.bShowAfterDeath) continue;
        const float Fade=FMath::Square(FMath::Clamp(1.f-float(Now-E.LocalTime)/.45f,0.f,1.f));
        Flash=FMath::Max(Flash,E.Strength*Fade);
    }
    if (Flash <= 0 && bDead) return;
    // A feathered rectangular ring keeps the center clear at every aspect ratio.
    const FLinearColor Edge(.72f,.002f,.001f,Flash),Clear(.72f,.002f,.001f,0);
    auto Boundary=[&](float Angle)
    {
        FVector2D D(FMath::Cos(Angle),FMath::Sin(Angle));
        return D/FMath::Max(FMath::Abs(D.X),FMath::Abs(D.Y))*FVector2D(W*.5f,H*.5f);
    };
    for(int32 I=0;I<64;++I)
    {
        const auto A=Boundary(I*2.f*PI/64),B=Boundary((I+1)*2.f*PI/64);
        Tri(Center+A,Center+B,Center+B*.70f,Edge,Edge,Clear);
        Tri(Center+A,Center+B*.70f,Center+A*.70f,Edge,Clear,Clear);
    }
    const FRotator Facing(0,Character->GetEffectiveAimRotation().Yaw,0);
    const FVector Forward=Facing.Vector(),Right=FRotationMatrix(Facing).GetUnitAxis(EAxis::Y);
    for(const auto& E:Events)
    {
        if (bDead) break;
        const float Alpha=FMath::Square(FMath::Clamp(1.f-float(Now-E.LocalTime)/.65f,0.f,1.f));
        const FVector Source=E.ToAttacker.GetSafeNormal2D();
        if(Alpha<=0 || Source.IsNearlyZero())continue; // Environmental damage has no compass bearing.
        const float Angle=FMath::Atan2(FVector::DotProduct(Source,Right),FVector::DotProduct(Source,Forward))-PI*.5f;
        // Front above, right to the right, rear below; rotates with current view.
        const float Radius=720.f*.17f*Scale;
        for(int32 Band=0;Band<2;++Band)
        {
            const float R=Radius+Band*7.f*Scale,Width=(Band?1.5f:4.f)*Scale;
            for(int32 I=0;I<32;++I)
            {
                const float A=Angle+FMath::DegreesToRadians(-34.f+68.f*I/32),B=Angle+FMath::DegreesToRadians(-34.f+68.f*(I+1)/32);
                const FVector2D DA(FMath::Cos(A),FMath::Sin(A)),DB(FMath::Cos(B),FMath::Sin(B));
                const float FadeA=FMath::Min3(1.f,I/3.f,(32-I)/3.f),FadeB=FMath::Min3(1.f,(I+1)/3.f,(31-I)/3.f);
                const FLinearColor CA(1.f,.012f,.003f,Alpha*FadeA*(Band?.65f:1.f)),CB=CA.CopyWithNewOpacity(Alpha*FadeB*(Band?.65f:1.f));
                Tri(Center+DA*R,Center+DB*R,Center+DB*(R+Width),CA,CB,CB);
                Tri(Center+DA*R,Center+DB*(R+Width),Center+DA*(R+Width),CA,CB,CA);
            }
        }
    }
    FCanvasTriangleItem Item(Triangles,GWhiteTexture);Item.BlendMode=SE_BLEND_Translucent;Canvas->DrawItem(Item);
}
