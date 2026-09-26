// Recovered Juno movement and track collision in Forest First, without a window.
#include "movement_scenario.h"
#include "integration_scenario.h"
#include <fstream>
#include <iostream>
#include <iomanip>

using namespace jfg_native;
static void save(const std::string &path, const std::vector<uint8_t> &pixels) {
    std::ofstream out(path, std::ios::binary); out.write(reinterpret_cast<const char *>(pixels.data()), pixels.size());
    if (!out) throw std::runtime_error("Cannot save native movement evidence");
}
int main(int argc, char **argv) {
    try {
        if (argc != 9)
            throw std::runtime_error("usage: jfg_native_movement CHARACTER SELECTOR REGION_MESH REGION_INFO COLLISION PHYSICS CAMERA OUTPUT_PREFIX");
        auto character = std::make_shared<const AssetPackage>(loadAssetPackage(argv[1]));
        const auto selection = readJunoSelection(argv[2]);
        const auto region = loadRegion(argv[3], argv[4]);
        const auto physics = readJunoPhysics(argv[6]);
        const auto collision = TrackCollision::load(argv[5], physics->math);
        const auto cameraData = readJunoCamera(argv[7]);
        const std::string prefix = argv[8];
        NativeSession session; session.boot(character, selection, region, physics, collision, cameraData);
        session.apply(movementScene(*region));
        const EntityHandle juno{1, 1};
        NativeRenderer renderer(std::vector<std::shared_ptr<const AssetPackage>>{character, region->mesh}, CameraPreset::World);
        auto instances = session.snapshot().renderInstances();
        const auto first = session.camera(juno)->view();
        save(prefix + ".background.rgba", renderer.draw({}, first));
        save(prefix + ".terrain.rgba", renderer.draw({instances[0]}, first));
        save(prefix + ".character.rgba", renderer.draw({instances[1]}, first));
        std::ofstream raw(prefix + ".rgba", std::ios::binary), trace(prefix + ".movement.json");
        if (!raw || !trace) throw std::runtime_error("Cannot create native movement output");
        trace << std::setprecision(9) << "{\"level\":" << region->level << ",\"ticks_per_second\":60,\"frames\":[";
        uint64_t previous = 0;
        uint32_t wallTicks = 0, airborneTicks = 0, moveChanges = 0;
        float lowest = 1e9f, highest = -1e9f;
        auto source = [&](uint64_t tick) {
            const auto *body = session.body(juno);
            if (!body) throw std::runtime_error("Juno body missing during the movement proof");
            if (body->wall533) ++wallTicks;
            if (body->state568 == 3) ++airborneTicks;
            TickInput input;
            ActorInput actor; actor.control = movementControl(tick);
            input.actors.push_back({juno, actor});
            return input;
        };
        const unsigned frames = MovementTicks / 2;
        for (unsigned frame = 0; frame < frames; ++frame) {
            const auto time = presentationTime(frame + 1, 30);
            session.advanceNanoseconds(time - previous, source); previous = time;
            const auto snapshot = session.snapshot();
            const auto &body = *session.body(juno);
            lowest = std::min(lowest, body.position.y); highest = std::max(highest, body.position.y);
            moveChanges = body.moveChanges;
            const auto &camera = *session.camera(juno);
            const auto &pixels = renderer.draw(snapshot.renderInstances(), camera.view());
            raw.write(reinterpret_cast<const char *>(pixels.data()), pixels.size());
            trace << (frame ? "," : "") << "{\"frame\":" << frame << ",\"tick\":" << snapshot.hostTick << ",\"x\":" << body.position.x
                  << ",\"y\":" << body.position.y << ",\"z\":" << body.position.z << ",\"vx\":" << body.velocity.x << ",\"vy\":" << body.velocity.y
                  << ",\"vz\":" << body.velocity.z << ",\"heading\":" << body.heading11C << ",\"roll\":" << body.orientation[2]
                  << ",\"speed04\":" << body.speed04 << ",\"state\":" << unsigned(body.state568) << ",\"floor\":" << unsigned(body.floor532)
                  << ",\"wall\":" << unsigned(body.wall533) << ",\"ceiling\":" << unsigned(body.ceiling534) << ",\"move\":" << body.move3B
                  << ",\"clip\":" << snapshot.entities[0].clip << ",\"progress\":" << body.progress28 << ",\"camera_yaw\":" << camera.yaw() << ",\"camera_pitch\":" << camera.angles[1]
                  << ",\"camera_x\":" << camera.position.x << ",\"camera_y\":" << camera.position.y << ",\"camera_z\":" << camera.position.z
                  << ",\"collision\":" << body.lastCollisionMask << "}";
        }
        const auto &body = *session.body(juno);
        trace << "],\"final\":{\"x\":" << body.position.x << ",\"y\":" << body.position.y << ",\"z\":" << body.position.z
              << "},\"lowest_y\":" << lowest << ",\"highest_y\":" << highest << ",\"wall_ticks\":" << wallTicks
              << ",\"airborne_ticks\":" << airborneTicks << ",\"move_changes\":" << moveChanges;
        session.stop(); const auto stopped = session.snapshot();
        trace << ",\"stopped\":" << (stopped.entities.empty() && !stopped.assets && !stopped.region ? "true" : "false")
              << ",\"original_movement\":true,\"original_camera\":true,\"object_collision\":false,\"emulation\":false}\n";
        std::ofstream report(prefix + ".json");
        report << "{\"status\":\"rendered_unreviewed\",\"api\":\"D3D11\",\"vendor\":" << renderer.vendor()
               << ",\"width\":640,\"height\":480,\"frame_count\":" << frames << ",\"region\":21,\"geometry\":17"
               << ",\"ticks\":" << MovementTicks << ",\"output_fps\":30,\"perspective\":true"
               << ",\"emulator_dependencies\":false,\"display_list_interpreter\":false}\n";
        if (!raw || !trace || !report) throw std::runtime_error("Cannot finish native movement output");
        std::cout << "Native Forest First movement: " << frames << " frames, " << MovementTicks << " original frames\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
