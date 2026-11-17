#pragma once

#include "CoreMinimal.h"

class AERPlayerController;
class SWidget;

namespace ERMatchGraph
{
constexpr int32 MenuPage = 12;
TSharedRef<SWidget> Create(AERPlayerController* Controller);
}
