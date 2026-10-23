-- Angles are degrees. Parsing flattens repeat into fixed C++ instructions.
-- This optional example is disabled in project.echo so it won't double the Boss attacks.
barrage {
    aim { target = "player" },
    repeat_pattern { times=3, body={
        fire { pattern="fan", count=12, speed=140 },
        wait { seconds=.8 },
        incremental { angle=12 }
    }},
    set_direction { angle=180 },
    fire { pattern="ring", count=24, speed=100 },
    wait { seconds=2 }
}
