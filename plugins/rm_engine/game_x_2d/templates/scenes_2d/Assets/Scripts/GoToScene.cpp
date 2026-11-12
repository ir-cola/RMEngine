#include "GoToScene.h"

void GoToScene::BeginPlay(rm::Entity self) {
    (void)self;
    time = 0;
    rm::Log::Info("シーン「" + rm::Game::SceneName() + "」(" + std::to_string(rm::Game::SceneIndex() + 1) + " / " +
                  std::to_string(rm::Game::SceneCount()) + ")。Enter で「" + next + "」へ");
}

void GoToScene::Tick(rm::Entity self, float dt) {
    (void)self;
    time += dt;
    // The switch happens at the end of this frame (this scene's EndPlay, then the next one's BeginPlay).
    if (rm::Input::Pressed(rm::Key::Enter) || (after > 0 && time >= after)) rm::Game::LoadScene(next);
}

void GoToScene::EndPlay(rm::Entity self) {
    (void)self;
    rm::Log::Info("シーン「" + rm::Game::SceneName() + "」を出ます");
}
