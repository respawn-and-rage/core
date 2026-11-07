#include "ERRecon.h"
#include "ERHUDLayout.h"
#include "ERPlayerController.h"
#include "ERCharacter.h"
#include "ERBlockRoom.h"
#include "EngineUtils.h"
#include "Engine/Canvas.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "CanvasItem.h"
#include "GlobalRenderResources.h"
#include "Misc/CommandLine.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
constexpr int32 DiscSegments=128;
struct FDiscGeometry
{
    FVector2D Points[DiscSegments+1],Normals[DiscSegments];
    float Limit=FMath::Cos(PI/DiscSegments);
    FDiscGeometry()
    {
        for(int32 I=0;I<=DiscSegments;++I)
        {
            const float Angle=I*2*PI/DiscSegments;
            Points[I]=FVector2D(FMath::Cos(Angle),FMath::Sin(Angle));
            if(I<DiscSegments)
            {
                const float Mid=(I+.5f)*2*PI/DiscSegments;
                Normals[I]=FVector2D(FMath::Cos(Mid),FMath::Sin(Mid));
            }
        }
    }
};
}

void AERReconDirector::DrawMinimap(AERHUD* HUD, AERPlayerController* PC)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ERRecon_Minimap);
    if (!HUD || !HUD->Canvas || !PC->Recon || PC->IsBlockMapLoading()) return;
    for (TActorIterator<AERBlockRoom> It(GetWorld()); It; ++It)
        if (!It->IsMapReady() || It->GetMapEpoch()!=Frame.GeometryEpoch) return;
    const auto& Data = PC->Recon->Snapshot;
    const auto* PS = PC->GetPlayerState<AERPlayerState>();
    if (!PS || Data.Team != PS->Team || Data.Team == EERTeam::None || Data.Epoch != Frame.Epoch) return;
    UCanvas* Canvas = HUD->Canvas;
    const FERHUDLayout Layout(FVector2D(Canvas->SizeX,Canvas->SizeY));
    const float Scale=Layout.Scale, Radius=Layout.MinimapRadius();
    const FVector2D Center=Layout.MinimapCenter();
    const float ViewSpan=ERServer::Get(GetWorld(),"Recon.MinimapRadius")*200;
    // Look ahead in the same yaw basis used by terrain, fog and contacts.
    // The owner sits 65% of the radius below center, leaving 35% to the rim.
    const float ViewYaw=PC->GetControlRotation().Yaw;
    const FVector ViewCenter=PC->GetPawn() ? PC->GetPawn()->GetActorLocation()+FRotator(0,ViewYaw,0).Vector()*(ViewSpan*.5f*.65f) : Frame.Center;
    const FVector Offset=(ViewCenter-Frame.Center)/Frame.Span;
    const FVector2D MapOrigin(Offset.Y,-Offset.X);
    const float MapScale=Frame.Span/ViewSpan;
    auto ToView = [&](const FVector2D& P) { return (P-MapOrigin)*(2.f*MapScale); };
    auto ToUV = [&](const FVector2D& P) { return P/(2.f*MapScale)+MapOrigin+FVector2D(.5); };
    static const FDiscGeometry Disc;
    const auto Color = ERRecon::TeamColor(Data.Team);
    // Use the same local heading as the owner's marker, every rendered frame.
    float Sin, Cos; FMath::SinCos(&Sin,&Cos,FMath::DegreesToRadians(ViewYaw));
    // Fog and the cached bird image retain their shared world-space grid.
    // Rotate every displayed position around the disc using the same basis.
    auto Orient = [&](const FVector2D& P) { return FVector2D(Cos*P.X+Sin*P.Y,-Sin*P.X+Cos*P.Y); };
    TArray<FCanvasUVTri> CircleTriangles;CircleTriangles.Reserve(DiscSegments*3);
    auto Circle = [&](FVector2D At,float R,FLinearColor Ink,float Feather=1.f)
    {
        Feather*=Scale;
        CircleTriangles.Reset();auto& Tris=CircleTriangles;
        auto Add = [&](FVector2D A,FLinearColor CA,FVector2D B,FLinearColor CB,FVector2D C,FLinearColor CC)
        {
            FCanvasUVTri Tri;
            Tri.V0_Pos=A; Tri.V1_Pos=B; Tri.V2_Pos=C;
            Tri.V0_UV=Tri.V1_UV=Tri.V2_UV=FVector2D::ZeroVector;
            Tri.V0_Color=CA; Tri.V1_Color=CB; Tri.V2_Color=CC; Tris.Add(Tri);
        };
        const auto Clear=Ink.CopyWithNewOpacity(0);
        for (int32 I=0; I<DiscSegments; ++I)
        {
            const FVector2D DA=Disc.Points[I],DB=Disc.Points[I+1];
            const FVector2D IA=At+DA*(R-Feather), IB=At+DB*(R-Feather), OA=At+DA*R, OB=At+DB*R;
            Add(At,Ink,IA,Ink,IB,Ink);
            Add(IA,Ink,OA,Clear,OB,Clear); Add(IA,Ink,OB,Clear,IB,Ink);
        }
        FCanvasTriangleItem Item(Tris,GWhiteTexture); Item.BlendMode=SE_BLEND_Translucent; Canvas->DrawItem(Item);
    };
    // A feathered circular shadow, followed by an antialiased dark frame.
    Circle(Center+FVector2D(0,4)*Scale,Radius+17*Scale,FLinearColor(0,0,0,.5),16);
    Circle(Center,Radius+5*Scale,FLinearColor(.005,.009,.014,.96),1.5);
    Circle(Center,Radius,FLinearColor(.015,.021,.028,1),1.25);
    const bool HasImage = CapturedEpoch == Frame.Epoch && BirdView && BirdView->GetResource();
    const FTexture* Texture=HasImage ? BirdView->GetResource() : GWhiteTexture;
    TArray<FCanvasUVTri> Tiles;Tiles.Reserve(1024);
    // Transform the existing world fog grid into the local fixed-scale view,
    // then clip terrain and fog together. UVs still address the whole-map capture.
    MinimapFog.Update(Data.Explored);
    MinimapFog.Visit(MapOrigin,MapScale,1.-1.25*Scale/Radius,[&](int32 CellX,int32 CellY,int32 Size)
    {
        const float X=float(CellX)/ERRecon::Grid-.5f;
        const float Y=float(CellY)/ERRecon::Grid-.5f;
        const float Step=float(Size)/ERRecon::Grid;
        TArray<FVector2D,TInlineAllocator<16>> Polygon;
        Polygon.Append({ToView(FVector2D(X,Y)),ToView(FVector2D(X+Step,Y)),ToView(FVector2D(X+Step,Y+Step)),ToView(FVector2D(X,Y+Step))});
        bool Inside=true; for (const auto& P:Polygon) Inside &= P.SizeSquared()<=.997;
        if (!Inside) for (int32 Edge=0; Edge<DiscSegments && Polygon.Num(); ++Edge)
        {
            const FVector2D Normal=Disc.Normals[Edge];
            const float Limit=Disc.Limit;
            TArray<FVector2D,TInlineAllocator<16>> Clipped;
            for (int32 I=0; I<Polygon.Num(); ++I)
            {
                const FVector2D A=Polygon[I], B=Polygon[(I+1)%Polygon.Num()];
                const float DA=FVector2D::DotProduct(A,Normal)-Limit, DB=FVector2D::DotProduct(B,Normal)-Limit;
                if (DA<=0) Clipped.Add(A);
                if ((DA<0 && DB>0) || (DA>0 && DB<0)) Clipped.Add(FMath::Lerp(A,B,DA/(DA-DB)));
            }
            Polygon=MoveTemp(Clipped);
        }
        for (int32 I=1; I+1<Polygon.Num(); ++I)
        {
            FCanvasUVTri Tri;
            Tri.V0_Pos=Center+Orient(Polygon[0])*Radius; Tri.V1_Pos=Center+Orient(Polygon[I])*Radius; Tri.V2_Pos=Center+Orient(Polygon[I+1])*Radius;
            Tri.V0_UV=ToUV(Polygon[0]); Tri.V1_UV=ToUV(Polygon[I]); Tri.V2_UV=ToUV(Polygon[I+1]);
            const float Shade=HasImage ? .85f : .22f;
            const auto EdgeInk = [&](const FVector2D& P)
            { return FLinearColor(Shade,Shade,Shade,FMath::Clamp((1.f-P.Size())*Radius/(1.25f*Scale),0.f,1.f)); };
            Tri.V0_Color=EdgeInk(Polygon[0]); Tri.V1_Color=EdgeInk(Polygon[I]); Tri.V2_Color=EdgeInk(Polygon[I+1]);
            Tiles.Add(Tri);
        }
    });
    if (Tiles.Num()) { FCanvasTriangleItem Item(Tiles,Texture); Item.BlendMode=SE_BLEND_Translucent; Canvas->DrawItem(Item); }
    auto Project = [&](const FVector& Position)
    {
        const FVector P=(Position-ViewCenter)/ViewSpan;
        return Center+Orient(FVector2D(P.Y,-P.X))*(2*Radius);
    };
    const auto* GS=GetWorld()->GetGameState();
    const double Now=GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
    // Packet loss must never turn the last enemy sample into a lasting tracker.
    LastDrawnEnemies=0;
    if (Now-Data.SampleTime<1) for (const auto& Contact:Data.Contacts)
    {
        FVector Position=Contact.Position;
        float Yaw=Contact.Yaw;
        if (Contact.Self && PC->GetPawn()) { Position=PC->GetPawn()->GetActorLocation(); Yaw=PC->GetControlRotation().Yaw; }
        FVector2D P=Project(Position);
        const float Distance=(P-Center).Size();
        if (Distance>Radius) continue;
        // Keep a known contact near the rim legible instead of discarding it
        // merely because its outlined marker would overlap the circular edge.
        if (Distance>Radius-9*Scale) P=Center+(P-Center)*((Radius-9*Scale)/Distance);
        const auto Ink=ERRecon::TeamColor(Contact.Team);
        if (Contact.Drone)
        {
            for (const FVector2D D : {FVector2D(1,1),FVector2D(1,-1)}) HUD->DrawLine(P.X-D.X*4*Scale,P.Y-D.Y*4*Scale,P.X+D.X*4*Scale,P.Y+D.Y*4*Scale,Ink,1.7f*Scale);
            continue;
        }
        const bool Enemy=Contact.Team!=Data.Team;
        if (Enemy)
        {
            ++LastDrawnEnemies;
            Circle(P,8*Scale,FLinearColor(.95,.95,.95,1)); Circle(P,6.7f*Scale,FLinearColor::Black);
            TArray<FCanvasUVTri> Diamond;
            for (int32 I=0;I<2;++I)
            {
                FCanvasUVTri Tri;
                Tri.V0_Pos=P+FVector2D(0,I?-5.2:5.2)*Scale; Tri.V1_Pos=P+FVector2D(I?5.2:-5.2,0)*Scale; Tri.V2_Pos=P+FVector2D(I?-5.2:5.2,0)*Scale;
                Tri.V0_UV=Tri.V1_UV=Tri.V2_UV=FVector2D::ZeroVector; Tri.V0_Color=Tri.V1_Color=Tri.V2_Color=Ink;
                Diamond.Add(Tri);
            }
            FCanvasTriangleItem Item(Diamond,GWhiteTexture); Item.BlendMode=SE_BLEND_Translucent; Canvas->DrawItem(Item);
        }
        else { Circle(P,(Contact.Self?7:5.5)*Scale,FLinearColor::Black); Circle(P,(Contact.Self?5.2:4.2)*Scale,Ink); }
        const float Angle=FMath::DegreesToRadians(Yaw);
        const FVector2D Forward=Orient(FVector2D(FMath::Sin(Angle),-FMath::Cos(Angle)));
        HUD->DrawLine(P.X,P.Y,P.X+Forward.X*10*Scale,P.Y+Forward.Y*10*Scale,Contact.Self?FLinearColor::White:Ink,1.5f*Scale);
    }
    if (LastHUDSample!=Data.SampleTime && FParse::Param(FCommandLine::Get(),TEXT("ERReconAudit")))
    {
        LastHUDSample=Data.SampleTime;
        UE_LOG(LogTemp,Display,TEXT("ER_RECON_HUD epoch=%u team=%d enemies_drawn=%d age=%.2f view_yaw=%.1f"),Frame.Epoch,int32(Data.Team),LastDrawnEnemies,Now-Data.SampleTime,ViewYaw);
    }
    TArray<FCanvasUVTri> Rim;Rim.Reserve(DiscSegments*4);
    for (int32 I=0; I<DiscSegments; ++I)
    {
        const FVector2D DA=Disc.Points[I],DB=Disc.Points[I+1];
        for (int32 Band=0; Band<2; ++Band)
        {
            const float Inner=Radius+(-1.5f+Band*1.5f)*Scale, Outer=Inner+1.5f*Scale;
            const auto CI=Color.CopyWithNewOpacity(Band ? .75f : 0.f), CO=Color.CopyWithNewOpacity(Band ? 0.f : .75f);
            FCanvasUVTri T;
            T.V0_Pos=Center+DA*Inner; T.V1_Pos=Center+DA*Outer; T.V2_Pos=Center+DB*Outer;
            T.V0_Color=CI; T.V1_Color=T.V2_Color=CO; Rim.Add(T);
            T.V1_Pos=T.V2_Pos; T.V2_Pos=Center+DB*Inner; T.V2_Color=CI; Rim.Add(T);
        }
    }
    FCanvasTriangleItem RimItem(Rim,GWhiteTexture); RimItem.BlendMode=SE_BLEND_Translucent; Canvas->DrawItem(RimItem);
    // Fixed respawn-facing bearing, never a tracker of enemy pawns or current player position.
    const auto* Character=Cast<AERCharacter>(PC->GetPawn());
    const float SpawnYaw=Character?Character->SpawnFacingYaw:(Data.Team==EERTeam::Alpha?0.f:180.f);
    const FVector2D BearingDirection=FERHUDLayout::SpawnBearing(SpawnYaw,ViewYaw);
    const auto EnemyColor=ERRecon::TeamColor(Data.Team==EERTeam::Alpha?EERTeam::Omega:EERTeam::Alpha);
    const FVector2D BearingTangent(-BearingDirection.Y,BearingDirection.X);
    FCanvasUVTri BearingTriangle;
    BearingTriangle.V0_Pos=Center+BearingDirection*(Radius+23*Scale);
    BearingTriangle.V1_Pos=Center+BearingDirection*(Radius+13*Scale)+BearingTangent*5*Scale;
    BearingTriangle.V2_Pos=Center+BearingDirection*(Radius+13*Scale)-BearingTangent*5*Scale;
    BearingTriangle.V0_UV=BearingTriangle.V1_UV=BearingTriangle.V2_UV=FVector2D::ZeroVector;
    BearingTriangle.V0_Color=BearingTriangle.V1_Color=BearingTriangle.V2_Color=EnemyColor.CopyWithNewOpacity(.95f);
    TArray<FCanvasUVTri> BearingTriangles;BearingTriangles.Add(BearingTriangle);
    // Mirror across the disc center to mark the player's own spawn side.
    FCanvasUVTri HomeTriangle=BearingTriangle;
    HomeTriangle.V0_Pos=2*Center-BearingTriangle.V0_Pos;
    HomeTriangle.V1_Pos=2*Center-BearingTriangle.V1_Pos;
    HomeTriangle.V2_Pos=2*Center-BearingTriangle.V2_Pos;
    HomeTriangle.V0_Color=HomeTriangle.V1_Color=HomeTriangle.V2_Color=Color.CopyWithNewOpacity(.95f);
    BearingTriangles.Add(HomeTriangle);
    FCanvasTriangleItem BearingItem(BearingTriangles,GWhiteTexture);
    BearingItem.BlendMode=SE_BLEND_Translucent;Canvas->DrawItem(BearingItem);
    if(HUD->DroneIcon && HUD->DroneIcon->GetResource())
    {
        const float Height=24*Scale,Width=Height*(1008.f/913.f),Gap=6*Scale;
        const float Left=Center.X-(ERRecon::Stock*Width+(ERRecon::Stock-1)*Gap)*.5f;
        const int32 Remaining=FMath::Clamp(Data.Remaining,0,ERRecon::Stock);
        for(int32 I=0;I<ERRecon::Stock;++I)
        {
            FCanvasTileItem Icon(FVector2D(Left+I*(Width+Gap),Center.Y+Radius+34*Scale),
                HUD->DroneIcon->GetResource(),FVector2D(Width,Height),
                FVector2D(113.f/1254.f,168.f/1254.f),FVector2D(1121.f/1254.f,1081.f/1254.f),
                Color.CopyWithNewOpacity(I<Remaining?.9f:.33f));
            Icon.BlendMode=SE_BLEND_Translucent;Canvas->DrawItem(Icon);
        }
    }
}
