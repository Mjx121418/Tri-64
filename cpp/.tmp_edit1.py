import re

p = 'tests/LevelScript/test_level_script.cpp'
s = open(p).read()

# 1) writeObj 改为单 Mesh
old = '''// \xe5\xb0\x86\xe7\xbd\x91\xe6\xa0\xbc\xe5\x90\x88\xe5\xb9\xb6\xe5\x90\x8e\xe5\x86\x99\xe5\x85\xa5 Wavefront OBJ\xef\xbc\x88\xe9\xa1\xb6\xe7\x82\xb9/UV/\xe6\xb3\x95\xe7\xba\xbf + \xe9\x9d\xa2\xe7\xb4\xa2\xe5\xbc\x95\xef\xbc\x89
void writeObj(const std::filesystem::path &path, const std::vector<GBI::Mesh> &meshes,
              const char *name) {
    std::filesystem::create_directories(path.parent_path());
    FILE *f = fopen(path.string().c_str(), "w");
    if (!f) {
        printf("test_export_obj: cannot open %s\\n", path.string().c_str());
        return;
    }
    fprintf(f, "# tri-64 DL export: %s\\n", name);
    fprintf(f, "o %s\\n", name);

    size_t base = 0;
    for (const auto &mesh : meshes) {
        for (const auto &v : mesh.vertices) {
            fprintf(f, "v %f %f %f\\n", v.position[0], v.position[1], v.position[2]);
        }
        for (const auto &v : mesh.vertices) {
            fprintf(f, "vt %f %f\\n", v.uv[0], v.uv[1]);
        }
        for (const auto &v : mesh.vertices) {
            fprintf(f, "vn %f %f %f\\n", v.normal[0], v.normal[1], v.normal[2]);
        }
        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            fprintf(f, "f %zu/%zu/%zu %zu/%zu/%zu %zu/%zu/%zu\\n",
                    mesh.indices[i] + 1 + base, mesh.indices[i] + 1 + base,
                    mesh.indices[i] + 1 + base,
                    mesh.indices[i + 1] + 1 + base, mesh.indices[i + 1] + 1 + base,
                    mesh.indices[i + 1] + 1 + base,
                    mesh.indices[i + 2] + 1 + base, mesh.indices[i + 2] + 1 + base,
                    mesh.indices[i + 2] + 1 + base);
        }
        base += mesh.vertices.size();
    }
    fclose(f);
}'''
new = '''// \xe5\x86\x99\xe5\x85\xa5 Wavefront OBJ\xef\xbc\x88\xe9\xa1\xb6\xe7\x82\xb9/UV/\xe6\xb3\x95\xe7\xba\xbf + \xe9\x9d\xa2\xe7\xb4\xa2\xe5\xbc\x95\xef\xbc\x89
void writeObj(const std::filesystem::path &path, const GBI::Mesh &mesh, const char *name) {
    std::filesystem::create_directories(path.parent_path());
    FILE *f = fopen(path.string().c_str(), "w");
    if (!f) {
        printf("test_export_obj: cannot open %s\\n", path.string().c_str());
        return;
    }
    fprintf(f, "# tri-64 DL export: %s\\n", name);
    fprintf(f, "o %s\\n", name);

    for (const auto &v : mesh.vertices) {
        fprintf(f, "v %f %f %f\\n", v.position[0], v.position[1], v.position[2]);
    }
    for (const auto &v : mesh.vertices) {
        fprintf(f, "vt %f %f\\n", v.uv[0], v.uv[1]);
    }
    for (const auto &v : mesh.vertices) {
        fprintf(f, "vn %f %f %f\\n", v.normal[0], v.normal[1], v.normal[2]);
    }
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        fprintf(f, "f %zu/%zu/%zu %zu/%zu/%zu %zu/%zu/%zu\\n",
                mesh.indices[i] + 1, mesh.indices[i] + 1, mesh.indices[i] + 1,
                mesh.indices[i + 1] + 1, mesh.indices[i + 1] + 1, mesh.indices[i + 1] + 1,
                mesh.indices[i + 2] + 1, mesh.indices[i + 2] + 1, mesh.indices[i + 2] + 1);
    }
    fclose(f);
}'''
assert old in s, 'writeObj block not found'
s = s.replace(old, new)
open(p, 'w').write(s)
print('writeObj replaced')
