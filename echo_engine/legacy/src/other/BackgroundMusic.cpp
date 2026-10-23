//
// Created by Bing on 2023/11/22.
//

#include "BackgroundMusic.hpp"

echo::other::BackgroundMusic::BackgroundMusic() = default;

echo::other::BackgroundMusic::~BackgroundMusic() = default;

echo::other::BackgroundMusic::BackgroundMusic(const echo::other::BackgroundMusic& music){
    *this = music;
}

echo::other::BackgroundMusic::BackgroundMusic(echo::other::BackgroundMusic&& music) noexcept {
    *this = std::move(music);
}

echo::other::BackgroundMusic& echo::other::BackgroundMusic::operator=(const echo::other::BackgroundMusic& music){
    if (this == &music)
        return *this;
    *this = music;
    return *this;
}

echo::other::BackgroundMusic& echo::other::BackgroundMusic::operator=(echo::other::BackgroundMusic&& music) noexcept {
    if (this == &music)
        return *this;
    *this = std::move(music);
    return *this;
}


void echo::other::BackgroundMusic::setMusic(std::filesystem::path&& filename, sf::Time&& start, sf::Time&& end){
    this->m_music_info.audioPath = filename;
    this->m_music_info.start_pos = start;
    this->m_music_info.end_pos = end;
    this->m_is_modify_music = true;

}
bool echo::other::BackgroundMusic::checkMusicModify(){
    if (this->m_is_modify_music){
        this->m_is_modify_music = false;
        return true;
    }
    return false;
}

echo::core::define::BackgroundMusic& echo::other::BackgroundMusic::getMusic() {
    return this->m_music_info;
}

void echo::other::BackgroundMusic::loopDetection(){
    if (this->checkMusicModify()){
        this->m_music.stop();

        if (!this->m_music.openFromFile(this->m_music_info.audioPath.string())){
            std::runtime_error("The audio path does not exist");
        }
        this->m_music.play();
    }

    if (this->m_music.getPlayingOffset() > this->m_music_info.end_pos) {
        this->m_music.setPlayingOffset(this->m_music_info.start_pos);
    }
}