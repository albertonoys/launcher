# SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
# SPDX-License-Identifier: MIT

# The startup sound reply runs on the sound player's worker thread. It must not touch LVGL
# (an lv_async_call from there corrupted LVGL's timer list and crashed the launcher); it only
# stores the result, which the startup sound timer handles on the LVGL thread.

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "main/ui/ui_launch_page_startup.cpp").read_text(encoding="utf-8")

play = SOURCE[SOURCE.index("void UILaunchPage::play_startup_sound_with_retry()"):]
play = play[:play.index("\n}\n")]
reply = play[play.index('cp0_signal_audio_api({"SystemSoundPlay", "0"}'):]
reply = reply[:reply.index("});")]

assert "lv_" not in reply, "the worker-thread reply must not call LVGL"
assert "state->result.store(code)" in reply

timer = SOURCE[SOURCE.index("void UILaunchPage::startup_sound_timer_cb"):]
timer = timer[:timer.index("\n}\n")]
assert "result.exchange(StartupSoundAsyncState::kNoResult)" in timer
assert timer.index("handle_startup_sound_result(code)") < timer.index("pending.load()")


if __name__ == "__main__":
    pass
