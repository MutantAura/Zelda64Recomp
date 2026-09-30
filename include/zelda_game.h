#ifndef __ZELDA_GAME_H__
#define __ZELDA_GAME_H__

#include <cstdint>
#include <span>
#include <vector>

namespace zelda64 {
    enum class Game {
        MM,
        OoT,
    };

    // Each game is built as its own executable, selected with the ZELDA64_GAME_OOT define.
#ifdef ZELDA64_GAME_OOT
    constexpr Game current_game = Game::OoT;
#else
    constexpr Game current_game = Game::MM;
#endif

    constexpr bool is_mm() { return current_game == Game::MM; }
    constexpr bool is_oot() { return current_game == Game::OoT; }

    void quicksave_save();
    void quicksave_load();
    std::vector<uint8_t> decompress_mm(std::span<const uint8_t> compressed_rom);
    std::vector<uint8_t> decompress_oot(std::span<const uint8_t> compressed_rom);
};

#endif
