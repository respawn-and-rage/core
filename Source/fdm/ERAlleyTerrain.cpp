#include "ERProceduralMap.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Components/InstancedStaticMeshComponent.h"

namespace ERProcedural
{
float AlleyBaseHeight(FVector Arena,FVector2D P,bool Balanced)
{
    if(Balanced)
    {
        // The whole spawn square rests on the upper terrace. Begin the ramp
        // beyond its inner edge, rather than forcing a flat pad into a slope.
        const float T=FMath::Clamp((FMath::Abs(P.Y)/(Arena.Y*.5f)-.30f)/.27f,0.f,1.f);
        return 180*T*T*(3-2*T);
    }
    const float Y=P.Y/Arena.Y+.5f;
    auto Smooth=[](float A,float B,float V){const float T=FMath::Clamp((V-A)/(B-A),0.f,1.f);return T*T*(3-2*T);};
    return 120*(1-Smooth(.16f,.28f,Y))+180*Smooth(.68f,.88f,Y);
}
static float AlleyGroundField(const FERProceduralLayout& L,FVector2D P)
{
    if(!L.GradedTerrain)return 0;
    if(L.LayoutSymmetry==EERTemplateSymmetry::Mirror)P.Y=-FMath::Abs(P.Y);
    auto BaseAt=[&](FVector2D Q)
    {
        if(!L.SeededDistrict)return AlleyBaseHeight(L.ArenaSize,Q);
        return AlleyBaseHeight(L.ArenaSize,Q,true);
    };
    if(L.SeededDistrict)
    {
        // Smoothly blend neighboring foundations instead of switching to the
        // nearest pad across a hard bisector in an open street.
        constexpr float Radius=1200;const float Base=BaseAt(P);
        float Sum=0,Weights=1/(Radius*Radius);
        for(int32 I=L.TerrainPads.Num()-1;I>=0;--I)
        {
            const auto& Pad=L.TerrainPads[I];
            const FVector2D Near(FMath::Clamp(P.X,Pad.Min.X,Pad.Max.X),FMath::Clamp(P.Y,Pad.Min.Y,Pad.Max.Y));
            const float D=FVector2D::Distance(P,Near);
            if(D<.001f)return Pad.Min.Z;
            if(D<Radius)
            {
                const float W=FMath::Square(1-D/Radius)/FMath::Square(D+1);
                Sum+=(Pad.Min.Z-Base)*W;Weights+=W;
            }
        }
        return Base+Sum/Weights;
    }
    if(FMath::Abs(P.Y-L.TownCrossing.Y)<350 && FMath::Abs(P.X-L.TownCrossing.X)<950)return 0;
    const float Base=AlleyBaseHeight(L.ArenaSize,P);float Distance=80,Height=Base;
    for(int32 I=L.TerrainPads.Num()-1;I>=0;--I)
    {
        const auto& Pad=L.TerrainPads[I];
        const FVector2D Closest(FMath::Clamp(P.X,Pad.Min.X,Pad.Max.X),FMath::Clamp(P.Y,Pad.Min.Y,Pad.Max.Y));
        const float D=FVector2D::Distance(P,Closest);
        if(D<Distance){Distance=D;Height=Pad.Min.Z;}
    }
    const float Weight=FMath::Square(1-Distance/80);
    return FMath::Lerp(Base,Height,Weight);
}
float AlleyGroundHeight(const FERProceduralLayout& L,FVector2D P)
{
    if(!L.VillageTerrainProfile.IsEmpty())return VillageGroundHeight(L,P);
    if(L.CircularArena)return CentralGroundHeight(L,P);
    for(const auto& R:L.UnderpassRamps)if(P.X>=R.Bounds.Min.X-.001 && P.X<=R.Bounds.Max.X+.001 && P.Y>=R.Bounds.Min.Y-.001 && P.Y<=R.Bounds.Max.Y+.001)return AlleyRampHeight(R,P);
    if(!L.SeededDistrict)return AlleyGroundField(L,P);
    // Use the same triangles as the collision mesh, not the ideal smooth
    // function between grid vertices. Spawns/routes then agree exactly with
    // visible terrain, including the edges of level foundations.
    const float W=L.ArenaSize.X+40,H=L.ArenaSize.Y+40;
    const int32 NX=FMath::CeilToInt(W/75),NY=FMath::CeilToInt(H/75);
    const float GX=FMath::Clamp((P.X/W+.5f)*NX,0.f,float(NX)),GY=FMath::Clamp((P.Y/H+.5f)*NY,0.f,float(NY));
    const int32 X=FMath::Min(FMath::FloorToInt(GX),NX-1),Y=FMath::Min(FMath::FloorToInt(GY),NY-1);
    const float U=GX-X,V=GY-Y;
    auto Sample=[&](int32 I,int32 J){return AlleyGroundField(L,{(float(I)/NX-.5f)*W,(float(J)/NY-.5f)*H});};
    if(U+V<=1){const float A=Sample(X,Y);return A+(Sample(X+1,Y)-A)*U+(Sample(X,Y+1)-A)*V;}
    const float D=Sample(X+1,Y+1);return D+(Sample(X,Y+1)-D)*(1-U)+(Sample(X+1,Y)-D)*(1-V);
}
void BuildAlleyTerrain(UDynamicMeshComponent* Component,const FERProceduralLayout& L)
{
    using UE::Geometry::FIndex3i;
    UE::Geometry::FDynamicMesh3 Mesh;Mesh.EnableAttributes();
    auto* Normals=Mesh.Attributes()->PrimaryNormals();auto* UVs=Mesh.Attributes()->PrimaryUV();
    const int32 NX=FMath::CeilToInt((L.ArenaSize.X+40)/75),NY=FMath::CeilToInt((L.ArenaSize.Y+40)/75);
    for(int32 Y=0;Y<=NY;++Y)for(int32 X=0;X<=NX;++X)
    {
        const FVector2D P((float(X)/NX-.5f)*(L.ArenaSize.X+40),(float(Y)/NY-.5f)*(L.ArenaSize.Y+40));
        const float H=AlleyGroundField(L,P),DX=(AlleyGroundField(L,P+FVector2D(10,0))-AlleyGroundField(L,P-FVector2D(10,0)))/20,
                    DY=(AlleyGroundField(L,P+FVector2D(0,10))-AlleyGroundField(L,P-FVector2D(0,10)))/20;
        Mesh.AppendVertex(FVector(P,H));Normals->AppendElement(FVector3f(FVector(-DX,-DY,1).GetSafeNormal()));UVs->AppendElement(FVector2f(P/250));
    }
    auto Triangle=[&](int32 A,int32 B,int32 C)
    {const int32 T=Mesh.AppendTriangle(A,B,C);Normals->SetTriangle(T,FIndex3i(A,B,C));UVs->SetTriangle(T,FIndex3i(A,B,C));};
    for(int32 Y=0;Y<NY;++Y)for(int32 X=0;X<NX;++X)
    {
        const FVector2D P(((X+.5f)/NX-.5f)*(L.ArenaSize.X+40),((Y+.5f)/NY-.5f)*(L.ArenaSize.Y+40));
        bool Cut=false;auto Inside=[&](const FBox& B){return P.X>B.Min.X && P.X<B.Max.X && P.Y>B.Min.Y && P.Y<B.Max.Y;};
        for(const auto& R:L.UnderpassRamps)Cut|=Inside(R.Bounds);for(const auto& B:L.UnderpassBreaches)Cut|=Inside(B);
        if(!Cut){const int32 A=Y*(NX+1)+X,B=A+1,C=A+NX+1,D=C+1;Triangle(A,C,B);Triangle(B,C,D);}
    }
    auto Quad=[&](FVector A,FVector B,FVector C,FVector D)
    {
        const FVector3f N(FVector::CrossProduct(B-A,C-A).GetSafeNormal());const int32 Start=Mesh.VertexCount();
        for(const auto& P:{A,B,C,D}){Mesh.AppendVertex(P);Normals->AppendElement(N);UVs->AppendElement(FVector2f(P.X/250,(P.Y+P.Z)/250));}
        Triangle(Start,Start+2,Start+1);Triangle(Start,Start+3,Start+2);
    };
    for(const auto& R:L.UnderpassRamps)
    {
        auto Low=[&](float X,float Y){return FVector(X,Y,AlleyRampHeight(R,{X,Y}));};
        const auto A=Low(R.Bounds.Min.X,R.Bounds.Min.Y),B=Low(R.Bounds.Max.X,R.Bounds.Min.Y),C=Low(R.Bounds.Max.X,R.Bounds.Max.Y),D=Low(R.Bounds.Min.X,R.Bounds.Max.Y);
        Quad(A,B,C,D);
        // Retaining faces meet the unchanged terrain at the sides. No hidden
        // upper plane crosses the open ramp or the destructible road panels.
        const bool AlongX=FMath::Abs(R.Exit.X-R.Entry.X)>1;
        const int32 Steps=FMath::CeilToInt(FVector::Dist2D(R.Entry,R.Exit)/75);
        for(int32 I=0;I<Steps;++I)for(float Side:{-1.f,1.f})
        {
            FVector2D P=FMath::Lerp(FVector2D(R.Entry),FVector2D(R.Exit),float(I)/Steps),Q=FMath::Lerp(FVector2D(R.Entry),FVector2D(R.Exit),float(I+1)/Steps);
            const int32 Across=AlongX ? 1:0;P[Across]=Q[Across]=Side<0 ? R.Bounds.Min[Across]:R.Bounds.Max[Across];
            const FVector A0(P,AlleyRampHeight(R,P)),B0(Q,AlleyRampHeight(R,Q)),C0(Q,AlleyGroundField(L,Q)),D0(P,AlleyGroundField(L,P));
            Quad(A0,B0,C0,D0);Quad(D0,C0,B0,A0);
        }
    }
    Component->SetMesh(MoveTemp(Mesh));Component->UpdateCollision(false);
}
bool FindAlleyRoofRimRoute(UWorld* World,const FERProceduralLayout& L,TArray<FVector>& Route)
{
    FCollisionQueryParams Q(SCENE_QUERY_STAT(ERAlleyRoofRim),false);
    for(TActorIterator<APawn> It(World);It;++It)Q.AddIgnoredActor(*It);
    const auto Capsule=FCollisionShape::MakeCapsule(34,92.5);
    for(const auto& H:L.Houses)if(H.RimHeight>0)
    {
        const auto Size=H.Frame.GetScale3D();const FTransform Frame(H.Frame.GetRotation(),H.Frame.GetLocation());
        for(int32 Face=0;Face<4;++Face)for(float Fraction:{-.3f,0.f,.3f})
        {
            const int32 Axis=Face/2;const float Sign=Face%2 ? 1:-1;
            FVector A(0,0,Size.Z+100),B=A;A[1-Axis]=B[1-Axis]=Size[1-Axis]*Fraction;
            A[Axis]=Sign*(Size[Axis]*.5-130);B[Axis]=Sign*(Size[Axis]*.5-20);B.Z+=H.RimHeight;
            A=Frame.TransformPosition(A);B=Frame.TransformPosition(B);
            FHitResult Hit;
            if(World->OverlapBlockingTestByChannel(A,FQuat::Identity,ECC_Pawn,Capsule,Q) || World->OverlapBlockingTestByChannel(B,FQuat::Identity,ECC_Pawn,Capsule,Q))continue;
            if(!World->LineTraceSingleByChannel(Hit,A,A-FVector(0,0,120),ECC_WorldStatic,Q) || FMath::Abs(Hit.ImpactPoint.Z-(A.Z-100))>1)continue;
            if(!World->LineTraceSingleByChannel(Hit,B,B-FVector(0,0,120),ECC_WorldStatic,Q) || !Hit.GetComponent()->ComponentHasTag(H.StructureName) || FMath::Abs(Hit.ImpactPoint.Z-(B.Z-100))>1)continue;
            if(World->SweepTestByChannel(A+FVector(0,0,30),B+FVector(0,0,30),FQuat::Identity,ECC_Pawn,Capsule,Q))continue;
            Route={A,B};return true;
        }
    }
    return false;
}
bool FindAlleyInteriorRoute(UWorld* World,const FERProceduralLayout& L,TArray<FVector>& Route)
{
    FCollisionQueryParams Q(SCENE_QUERY_STAT(ERAlleyDoor),false);
    for(TActorIterator<APawn> It(World);It;++It)Q.AddIgnoredActor(*It);
    const auto Capsule=FCollisionShape::MakeCapsule(34,92.5);
    for(const auto& House:L.Houses)for(const auto& Entry:House.Entrances)
    {
        bool Clear=true;FHitResult Hit;
        for(int32 I=1;I<Entry.Num();++I)Clear&=!World->SweepSingleByChannel(Hit,Entry[I-1],Entry[I],FQuat::Identity,ECC_Pawn,Capsule,Q);
        if(Clear){Route=Entry;for(int32 I=Entry.Num()-2;I>=0;--I)Route.Add(Entry[I]);return true;}
    }
    for(bool Covered:{true,false})for(const auto& House:L.Houses)
    {
        if(!House.Mesh)continue;
        const FVector Size=House.Frame.GetScale3D();
        auto Point=[&](FVector P){return House.Frame.TransformPosition(P);};
        for(int32 Axis=0;Axis<2;++Axis)for(float Sign:{-1.f,1.f})for(float Along:{-.35f,-.25f,-.15f,-.05f,.05f,.15f,.25f,.35f})
        {
            FVector A(Along,Along,0),B=A;A[Axis]=Sign*(.5f+75/Size[Axis]);B[Axis]=Sign*(.5f-150/Size[Axis]);
            A=Point(A);B=Point(B);FHitResult Hit;
            bool Street=true;for(const auto& Foot:L.TownBuildings)
                if(A.X>Foot.Min.X-35 && A.X<Foot.Max.X+35 && A.Y>Foot.Min.Y-35 && A.Y<Foot.Max.Y+35){Street=false;break;}
            if(!Street)continue;
            auto Stand=[&](FVector& P)
            {
                if(!World->LineTraceSingleByChannel(Hit,P+FVector(0,0,170),P-FVector(0,0,80),ECC_WorldStatic,Q))return false;
                P.Z=Hit.ImpactPoint.Z+94;return !World->OverlapBlockingTestByChannel(P+FVector(0,0,3),FQuat::Identity,ECC_Pawn,Capsule,Q);
            };
            if(!Stand(A) || !Stand(B) || FMath::Abs(A.Z-B.Z)>30)continue;
            if(Covered)
            {
                if(!World->LineTraceSingleByChannel(Hit,B+FVector(0,0,94),B+FVector(0,0,Size.Z+100),ECC_WorldStatic,Q))continue;
                const auto* Ceiling=Cast<UInstancedStaticMeshComponent>(Hit.GetComponent());
                if((!Ceiling || Ceiling->GetStaticMesh()!=House.Mesh) && !Hit.GetComponent()->ComponentHasTag(House.Mesh->GetFName()))continue;
            }
            FVector From=A,To=B;From.Z=To.Z=FMath::Max(A.Z,B.Z)+3;
            if(World->SweepSingleByChannel(Hit,From,To,FQuat::Identity,ECC_Pawn,Capsule,Q))continue;
            Route={A,B,A};return true;
        }
    }
    return false;
}
}
