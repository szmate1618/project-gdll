#include "frame_profile.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
struct Query { bool issued = false, ready = false; GLuint64 value = 0; };
std::unordered_map<GLuint,Query> queries;
std::vector<GLuint> issued;
GLuint nextId = 1;
GLint bits = 64;
int reads = 0;
void check() {
    const auto serial = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() / ("gdll-profile-"+std::to_string(serial)+".csv");
    {
        viewer::FrameProfile profile(path);
        // Saturate the ring. A 17th frame must preserve CPU data without reading
        // an unavailable GPU result or overwriting any pending timestamp pair.
        for (int frame = 0; frame < 17; ++frame) {
            profile.beginGpu(); profile.endGpu();
            profile.record({static_cast<std::uint64_t>(frame),1,2,3,4,5});
        }
        require(profile.pending()==16 && issued.size()==32 && reads==0,
                "Saturated profile reused a pending slot or read an unavailable query");
        // Complete a later frame first; its CPU record must stay associated.
        queries.at(issued[4]).ready = queries.at(issued[5]).ready = true;
        queries.at(issued[4]).value = 1000000; queries.at(issued[5]).value = 4500000;
        profile.collect();
        require(reads==2 && profile.pending()==15,"Ready timestamp pair was not collected");
        profile.beginGpu(); profile.endGpu(); profile.record({17,6,7,8,9,10});
        require(issued.size()==34 && profile.pending()==16,"Ready slot was not reused");
    }
    require(queries.empty(),"Profile leaked query objects");
    std::ifstream input(path); std::string line; std::getline(input,line);
    std::map<int,std::vector<double>> frames;
    while (std::getline(input,line)) {
        std::istringstream row(line); std::string field; std::getline(row,field,','); const int frame=std::stoi(field);
        std::vector<double> values; while(std::getline(row,field,',')) values.push_back(std::stod(field));
        require(frames.emplace(frame,values).second,"CSV duplicated a frame");
    }
    require(frames.size()==18,"CSV lost CPU records when GPU results were unavailable");
    require(frames.at(2).size()==7 && frames.at(2)[0]==3.5 && frames.at(2)[5]==10 && frames.at(2)[6]==5,
            "CSV lost frame association, time units, CPU sum or presentation separation");
    require(std::isnan(frames.at(16)[0]) && frames.at(17)[5]==30 && frames.at(17)[6]==10,
            "Missing GPU measurements or reused CPU records were incorrect");
    input.close();
    std::filesystem::remove(path);
    bits=0;reads=0;issued.clear();
    {
        viewer::FrameProfile profile(path); profile.beginGpu(); profile.endGpu(); profile.record({0,1,2,3,4,5});
        require(profile.pending()==0 && issued.empty() && reads==0,"Unsupported timestamps touched GPU queries");
    }
    std::filesystem::remove(path);
}
} // namespace

// Fake entry points enforce nonblocking query use without needing a GPU.
extern "C" {
void APIENTRY glGetQueryiv(GLenum target,GLenum name,GLint* value) {
    require(target==GL_TIMESTAMP && name==GL_QUERY_COUNTER_BITS,"Unexpected timestamp capability query"); *value=bits;
}
void APIENTRY glGenQueries(GLsizei count,GLuint* ids) {
    for(int i=0;i<count;++i) {ids[i]=nextId++;queries.emplace(ids[i],Query{});}
}
void APIENTRY glDeleteQueries(GLsizei count,const GLuint* ids) {
    for(int i=0;i<count;++i) require(queries.erase(ids[i])==1,"Deleted unknown query");
}
void APIENTRY glQueryCounter(GLuint id,GLenum target) {
    require(target==GL_TIMESTAMP && !queries.at(id).issued,"Reused an unread GPU query");
    queries.at(id)={true,false,0};issued.push_back(id);
}
void APIENTRY glGetQueryObjectiv(GLuint id,GLenum name,GLint* value) {
    require(name==GL_QUERY_RESULT_AVAILABLE && queries.at(id).issued,"Unexpected blocking GPU query");
    *value=queries.at(id).ready ? GL_TRUE : GL_FALSE;
}
void APIENTRY glGetQueryObjectui64v(GLuint id,GLenum name,GLuint64* value) {
    auto& query=queries.at(id);
    require(name==GL_QUERY_RESULT && query.issued && query.ready,"Unavailable GPU result would stall the CPU");
    *value=query.value;query.issued=false;++reads;
}
}
int main() {
    try {check();std::cout<<"Frame profiling preserves CPU records and never waits for GPU results\n";}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
