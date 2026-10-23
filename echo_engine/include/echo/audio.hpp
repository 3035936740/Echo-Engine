#pragma once
#include "resources.hpp"
#include <memory>
namespace echo {
class Audio {
    struct Impl;
    std::unique_ptr<Impl> p_;

  public:
    Audio();
    ~Audio();
    void load(const Resources&, const ProjectConfig&);
    void configure(WorldState&, const ProjectConfig&) const;
    bool available() const;
    void update(const WorldState&);
    void pause(bool);
    // Output-only editor preference; never changes snapshotted music transport.
    void setMusicMuted(bool);
    bool musicMuted() const;
};
} // namespace echo
