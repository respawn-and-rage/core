#include "ERMapLighting.h"
#include "ERProceduralMap.h"

namespace
{
struct FLampBuilder
{
    TArray<FERCellPanelDesc>& Panels;
    TArray<FERMapLamp>& Lamps;
    UMaterialInterface* LampSurface;
    float CellSize;
    int32 NextId = 0;

    FLampBuilder(TArray<FERCellPanelDesc>& InPanels, TArray<FERMapLamp>& InLamps,
        UMaterialInterface* InSurface, float InCellSize)
        : Panels(InPanels), Lamps(InLamps), LampSurface(InSurface), CellSize(InCellSize)
    {
        for (const auto& Panel : Panels) NextId = FMath::Max(NextId, Panel.Id + 1);
    }

    int32 AddPanel(const FVector& Center, const FQuat& Rotation, const FVector& Size, bool Lens,
        UMaterialInterface* Surface = nullptr)
    {
        const int32 Index = Panels.Num();
        auto& Panel = Panels.AddDefaulted_GetRef();
        Panel.Id = NextId++;
        Panel.Transform = FTransform(Rotation, Center - Rotation.RotateVector(Size * .5));
        Panel.Size = Size;
        Panel.Material = Lens ? EERBlockMaterial::Glass : EERBlockMaterial::Metal;
        Panel.Cell = Lens ? Size : FVector(FERCellWorld::FitCell(Size.X, CellSize),
            FERCellWorld::FitCell(Size.Y, CellSize), FERCellWorld::FitCell(Size.Z, CellSize));
        Panel.bPane = Lens;
        Panel.bAnchored = Lens;
        Panel.bAnchorBottom = !Lens;
        Panel.Surface = Lens ? LampSurface : Surface;
        Panel.Strength = Lens ? 1.f : 4.f;
        return Index;
    }

    void Ceiling(const FVector& Center, float Radius, float Candelas)
    {
        auto& Lamp = Lamps.AddDefaulted_GetRef();
        Lamp.Lenses.Add(AddPanel(Center, FQuat::Identity, {60,60,4}, true));
        Lamp.Position = Center - FVector(0,0,12);
        Lamp.Radius = Radius;
        Lamp.Candelas = Candelas;
    }

    void Ceiling(const FVector& Center) { Ceiling(Center, 750.f, 8.f); }

    void CeilingGrid(FVector Center, FVector2D Size, FQuat Frame = FQuat::Identity,
        float Radius = 750.f, float Candelas = 8.f, const TArray<FERLayoutFixedPiece>* Mounts = nullptr)
    {
        const int32 Columns = FMath::Clamp(FMath::CeilToInt32(Size.X / 1100.), 1, 8);
        const int32 Rows = FMath::Clamp(FMath::CeilToInt32(Size.Y / 1100.), 1, 8);
        for (int32 Y = 0; Y < Rows; ++Y)
            for (int32 X = 0; X < Columns; ++X)
            {
                FVector Point = Center + Frame.RotateVector(FVector((X+.5)/Columns*Size.X - Size.X*.5,
                    (Y+.5)/Rows*Size.Y - Size.Y*.5, 0));
                if (Mounts)
                {
                    // Skip stair openings and terraces. Use the underside of the actual slab.
                    const auto* Slab = Mounts->FindByPredicate([&](const FERLayoutFixedPiece& Piece)
                    {
                        if (Piece.Mesh || Piece.VisualOnly || Piece.CollisionOnly || Piece.PawnBarrier) return false;
                        const FVector Half = Piece.Transform.GetScale3D() * 50.;
                        const FVector Local = Piece.Transform.InverseTransformPositionNoScale(Point);
                        return Half.Z <= 55. && FMath::Abs(Local.Z + Half.Z) <= 8. &&
                            FMath::Abs(Local.X) + 60. <= Half.X && FMath::Abs(Local.Y) + 60. <= Half.Y;
                    });
                    if (!Slab) continue;
                    Point.Z = Slab->Transform.GetLocation().Z - Slab->Transform.GetScale3D().Z * 50. - 3.;
                }
                Ceiling(Point, Radius, Candelas);
            }
    }

    void Perimeter(const FVector& Arena, bool Circular, UMaterialInterface* Steel)
    {
        constexpr double Top = 1400;
        constexpr double PoleBottom = -500;
        constexpr double PoleHeight = Top - PoleBottom;
        for (int32 Index = 0; Index < 6; ++Index)
        {
            const double Angle = Index * UE_DOUBLE_PI / 3.;
            FVector Unit(FMath::Cos(Angle), FMath::Sin(Angle), 0);
            if (!Circular) Unit /= FMath::Max(FMath::Abs(Unit.X), FMath::Abs(Unit.Y));
            const FVector Head(Unit.X * (Arena.X*.5+65), Unit.Y * (Arena.Y*.5+65), Top);
            auto& Lamp = Lamps.AddDefaulted_GetRef();
            Lamp.Pole = AddPanel(Head - FVector(0,0,PoleHeight*.5), FQuat::Identity, {30,30,PoleHeight}, false, Steel);
            const FRotator Aim = (FVector(0,0,30) - (Head + FVector(0,0,66))).Rotation();
            Lamp.Direction = FRotator(FMath::Clamp(Aim.Pitch, -24., -12.), Aim.Yaw, 0);
            const FQuat Frame = Lamp.Direction.Quaternion();
            // Tilt around the bottom edge, embedded slightly in the steel post for a structural joint.
            const FVector PanelCenter = Head + Frame.GetUpVector() * 62.;
            Lamp.Position = PanelCenter + Frame.GetForwardVector() * 24;
            Lamp.Radius = float(Head.Size() * 1.20);
            Lamp.Floodlight = true;
            const bool MainBank = Index % 3 == 0;
            Lamp.Candelas = float(1.5 * (MainBank ? .68 : .24) * FMath::Square(Head.Size() / 100.));
            // A steel square backing joins the four standard 60 cm lenses.
            Panels[AddPanel(PanelCenter, Frame, {15,132,132}, false, Steel)].bAnchorBottom = false;
            for (double Y : {-33.,33.})
                for (double Z : {-33.,33.})
                {
                    const int32 Lens = AddPanel(PanelCenter + Frame.RotateVector(FVector(9.5,Y,Z)), Frame, {4,60,60}, true);
                    Panels[Lens].bAnchored = false;
                    Lamp.Lenses.Add(Lens);
                }
        }
    }
};
}

int32 FERMapLamp::WorkingLenses(const FERCellWorld& Cells) const
{
    if (Pole != INDEX_NONE)
    {
        const auto& Panel = Cells.GetPanels()[Pole];
        bool TopAlive = false;
        for (int32 Y = 0; Y < Panel.Dims.Y && !TopAlive; ++Y)
            for (int32 X = 0; X < Panel.Dims.X && !TopAlive; ++X)
                TopAlive = Cells.IsAlive(Pole, X, Y, Panel.Dims.Z-1);
        if (!TopAlive) return 0;
    }
    int32 Count = 0;
    for (const int32 Lens : Lenses) Count += Cells.IsAlive(Lens, 0, 0, 0) ? 1 : 0;
    return Count;
}

void ERMapLighting::Build(FERProceduralLayout& Layout, const UERMapTemplate& Recipe, float CellSize,
    UMaterialInterface* LampSurface, bool Night, TArray<FERMapLamp>& Lamps)
{
    FLampBuilder Builder(Layout.Panels, Lamps, LampSurface, CellSize);
    if (Recipe.Archetype == EERMapArchetype::Rooms)
    {
        Layout.FixedPieces.RemoveAll([&](const FERLayoutFixedPiece& Piece)
        {
            if (!Piece.VisualOnly || !Piece.Surface || Piece.Surface != Recipe.Rooms.LampSurface) return false;
            Builder.Ceiling(Piece.Transform.GetLocation());
            return true;
        });
        return;
    }
    if (Recipe.Archetype == EERMapArchetype::FireTraining)
    {
        // Maintain a low level of light at the floor even in taller warehouse variants.
        const float Height = Layout.IndoorCeiling;
        Builder.CeilingGrid({0,0,Height-5}, {Layout.WarehouseWidth,Layout.WarehouseLength},
            FQuat::Identity, Height*1.25f, .6f*FMath::Square(Height/100.f));
        return;
    }
    if (Night)
    {
        auto* Steel = Layout.ThemeMetalDetails;
        if (!Steel && Layout.ThemeMaterials.IsValidIndex(int32(EERBlockMaterial::Metal)))
            Steel = Layout.ThemeMaterials[int32(EERBlockMaterial::Metal)];
        if (!Steel) Steel = Recipe.CentralBuilding.SteelSurface.Get();
        Builder.Perimeter(Layout.ArenaSize, Layout.CircularArena, Steel);
    }
    for (const FVector& Mount : Layout.CentralCeilingMounts) Builder.Ceiling(Mount);
    if (Recipe.Archetype == EERMapArchetype::CentralBuilding && !Layout.CircularArena)
        for (int32 Floor = 0; Floor < Layout.BuildingFloors; ++Floor)
        {
            const FVector2D Setback = Layout.TerracedBuilding ? FVector2D(Floor*500.,Floor*200.) : FVector2D::ZeroVector;
            const FVector2D Size = FVector2D(Layout.CentralBuildingSize) - Setback - FVector2D(600,120);
            Builder.CeilingGrid(Layout.CentralBuildingCenter + FVector(0,0,(Floor+1)*320.-24.), Size,
                FQuat::Identity, 750.f, 8.f, &Layout.FixedPieces);
        }
    for (const auto& Building : Layout.SecondaryStructures)
    {
        double SecondaryOffset = 8.;
        if (Recipe.Archetype == EERMapArchetype::CentralBuilding)
            SecondaryOffset = Layout.CircularArena ? Building.Size.Z-300.+3. : 6.;
        for (int32 Floor = 1; Floor <= Building.Floors; ++Floor)
            Builder.CeilingGrid(Building.Frame.TransformPosition({0,0,Building.Size.Z*Floor/Building.Floors-SecondaryOffset}),
                FVector2D(Building.Size), Building.Frame.GetRotation());
    }
    for (const auto& Base : Layout.Bases)
        Builder.CeilingGrid(Base.Point({0,0,Base.RoofZ-10.}), FVector2D(Base.Size), Base.Frame.GetRotation());
    for (const auto& Bay : Layout.VillageClosedGround)
    {
        FVector2D Center = FVector2D::ZeroVector;
        for (const FVector2D& Point : Bay) Center += Point;
        if (!Bay.IsEmpty())
        {
            Center /= Bay.Num();
            Builder.Ceiling(FVector(Center, Recipe.Village.DeckHeight - 12.));
        }
    }
    for (const auto& House : Layout.Houses)
    {
        const FVector Size = House.Frame.GetScale3D();
        Builder.CeilingGrid(House.Frame.TransformPositionNoScale({0,0,Size.Z-26.}), FVector2D(Size),
            House.Frame.GetRotation());
    }
    if (Recipe.Archetype == EERMapArchetype::UrbanTown)
        for (const auto& Piece : Layout.FixedPieces)
    {
        if (!Piece.Roof || Piece.VisualOnly || Piece.Mesh) continue;
        const FVector Size = Piece.Transform.GetScale3D() * 100.;
        Builder.CeilingGrid(Piece.Transform.GetLocation() - FVector(0,0,Size.Z*.5+6.),
            FVector2D(Size), Piece.Transform.GetRotation());
    }
}

void ERMapLighting::BuildDepot(float CellSize, UMaterialInterface* LampSurface,
    TArray<FERCellPanelDesc>& Panels, TArray<FERMapLamp>& Lamps)
{
    FLampBuilder Builder(Panels, Lamps, LampSurface, CellSize);
    Builder.Perimeter({5600,3600,0}, false, nullptr);
}
