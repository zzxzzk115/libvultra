#include <vultra/core/event/event_center.hpp>

#include <iostream>

using namespace vultra;

struct PlayerScored
{
    int playerId;
    int score;
};

class GameSystem
{
public:
    void onPlayerScored(const PlayerScored& e)
    {
        std::cout << "Player " << e.playerId << " scored " << e.score << " points!\n";
        m_TotalScore += e.score;
        std::cout << "Total Score: " << m_TotalScore << "\n";
    }

private:
    int m_TotalScore = 0;
};

class AchievementSystem
{
public:
    AchievementSystem() : m_HighScore(0) { VULTRA_EVENT_SUBSCRIBE(PlayerScored, AchievementSystem, onPlayerScored); }

    ~AchievementSystem() { VULTRA_EVENT_UNSUBSCRIBE(PlayerScored, AchievementSystem, onPlayerScored); }

    void onPlayerScored(const PlayerScored& e)
    {
        if (e.score > m_HighScore)
        {
            std::cout << "Player " << e.playerId << " achieved a new high score: " << e.score << " points!\n";
            m_HighScore = e.score;
        }
    }

private:
    int m_HighScore = 0;
};

int main()
{
    GameSystem        gameSystem;
    AchievementSystem achievementSystem;

    VULTRA_EVENT_SUBSCRIBE_INSTANCE(PlayerScored, GameSystem, onPlayerScored, gameSystem);
    VULTRA_EVENT_EMIT_NOW(PlayerScored, 1, 100);

    return 0;
}