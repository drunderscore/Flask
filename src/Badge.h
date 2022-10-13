// The Badge pattern as taught by Andreas Kling
// https://awesomekling.github.io/Serenity-C++-patterns-The-Badge/

#pragma once

template<typename T>
class Badge
{
    friend T;
    Badge() = default;
};
