// core::starters -- see Starters.hpp.
#include "core/Starters.hpp"

#include <algorithm>
#include <initializer_list>
#include <utility>

namespace tidy::core::starters {
namespace {

using templates::Folder;
using templates::Role;
using templates::Route;
using templates::Template;

void routes(Template& t, std::initializer_list<const char*> exts, const char* folder) {
    // A pattern already routed (a kind's own, set first) is not added again:
    // the kind's route wins, and a pattern twice is a template problem.
    for (const char* e : exts) {
        const std::string pat = std::string("*.") + e;
        if (std::none_of(t.routes.begin(), t.routes.end(), [&](const Route& r) { return r.pattern == pat; }))
            t.routes.push_back({pat, folder});
    }
}

// The base tree, Scott's (s010a): the shell less fixes/, then the creative
// folders. Assets/ holds raw material by medium.
Template base(const std::string& name, const std::string& category) {
    Template t;
    t.name = name;
    t.category = category;
    t.folders = {
        {"backups", Role::History}, {"docs", Role::Input}, {"scripts", Role::Input},
        {"Notes", Role::Input},     {"Research", Role::Input},
        {"Assets", Role::Input},
        {"Assets/Graphics", Role::Input}, {"Assets/Photos", Role::Input}, {"Assets/Audio", Role::Input},
        {"Assets/Video", Role::Input},    {"Assets/Fonts", Role::Input},
        {"Work", Role::Product},    {"Drafts", Role::History},
        {"Final", Role::Product},   {"Promo", Role::Product},
    };
    return t;
}

// The base's routes -- appended after a kind's own, so the kind's win.
void base_routes(Template& t) {
    routes(t, {"curvz", "svg"}, "Assets/Graphics");
    routes(t, {"png", "jpg", "jpeg", "tif", "tiff", "heic"}, "Assets/Photos");
    routes(t, {"wav", "mp3", "flac", "m4a"}, "Assets/Audio");
    routes(t, {"mp4", "mov", "mkv"}, "Assets/Video");
    routes(t, {"ttf", "otf", "woff", "woff2"}, "Assets/Fonts");
    routes(t, {"scriv"}, "Work");
    routes(t, {"md", "txt"}, "Notes");
    // pdf / epub: no route -- research or a final export; the file's row asks.
}

void add(Template& t, std::initializer_list<const char*> paths, Role role = Role::Input) {
    for (const char* p : paths) t.folders.push_back({p, role});
}

// The order templates keeps (tree order, parents first): its own parse.
void sorted(Template& t) { t = templates::parse(templates::format(t)); }

std::vector<Starter> make() {
    std::vector<Starter> out;
    auto kind = [&](const char* name, const char* category, const char* about, auto extra) {
        Template t = base(name, category);
        extra(t);              // the kind's folders + routes, in front
        base_routes(t);
        sorted(t);
        out.push_back({std::move(t), about});
    };
    kind("creative-project", "Creative", "The base tree every creative kind shares", [](Template&) {});
    kind("3d-project", "3D", "Models, textures and renders; .blend in Work", [](Template& t) {
        add(t, {"Assets/Models", "Assets/Textures", "Assets/Renders"});
        routes(t, {"blend"}, "Work");
        routes(t, {"obj", "fbx", "glb", "gltf", "stl"}, "Assets/Models");
        routes(t, {"exr", "hdr"}, "Assets/Textures");
        routes(t, {"png"}, "Assets/Renders");
    });
    kind("art-project", "Art", "Paintings and drawings; brushes kept with the work", [](Template& t) {
        add(t, {"Assets/Brushes"});
        routes(t, {"kra", "xcf", "psd"}, "Work");
        routes(t, {"abr", "gbr", "myb"}, "Assets/Brushes");
    });
    kind("cad-project", "CAD", "Parts and drawings; the model in Work", [](Template& t) {
        add(t, {"Assets/Parts", "Assets/Drawings"});
        routes(t, {"fcstd", "scad"}, "Work");
        routes(t, {"step", "stp", "stl", "iges", "igs"}, "Assets/Parts");
        routes(t, {"dxf", "dwg"}, "Assets/Drawings");
    });
    kind("design-project", "Design", "Client design work: briefs, comps, deliverables", [](Template& t) {
        add(t, {"Notes/Briefs"});
        routes(t, {"curvz", "svg", "ai", "indd", "afdesign"}, "Work");
    });
    kind("music-project", "Music", "Songs and podcasts: samples, stems, the session in Work", [](Template& t) {
        add(t, {"Assets/Samples", "Assets/Stems"});
        routes(t, {"als", "flp", "rpp", "ardour", "mmpz", "logicx"}, "Work");
        routes(t, {"wav", "flac"}, "Assets/Samples");
    });
    kind("photo-project", "Photos", "A shoot: raw captures, selects, edits", [](Template& t) {
        add(t, {"Assets/Raw", "Assets/Selects"});
        routes(t, {"cr2", "cr3", "nef", "dng", "arw", "raf", "orf", "rw2", "xmp"}, "Assets/Raw");
    });
    kind("video-project", "Videos", "Footage in, the edit in Work, exports in Final", [](Template& t) {
        add(t, {"Assets/Footage"});
        routes(t, {"kdenlive", "prproj", "drp", "osp"}, "Work");
        routes(t, {"mp4", "mov", "mkv", "mts"}, "Assets/Footage");
    });
    kind("writing-project", "Writing", "Books, essays, stories; manuscripts in Work", [](Template& t) {
        routes(t, {"odt", "docx"}, "Work");
    });
    return out;
}

}  // namespace

const std::vector<Starter>& all() {
    static const std::vector<Starter> s = make();
    return s;
}

const Starter* find(const std::string& name) {
    for (const auto& s : all())
        if (s.t.name == name) return &s;
    return nullptr;
}

}  // namespace tidy::core::starters
