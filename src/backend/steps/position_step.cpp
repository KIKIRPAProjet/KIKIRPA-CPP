#include "steps/position_step.h"
#include "core/config/pipeline_config.h"
#include "core/data/tile.h"
#include <cmath>
#include <map>
#include <queue>
#include <set>
#include <sstream>

namespace StepsNs {
PositionStepClass::PositionStepClass() {
    ConfigNs::StepConfig c; c.name="position"; c.inputs={{"Tiles","tiles"},{"PairMatches","matches"}}; c.outputs={{"Positions","positions"}}; configJson=c;
}
bool PositionStepClass::execute() {
    result=Json::object();
    if (!dataStore) { result["error"]="position: DataStore is not attached"; return false; }
    try {
        const auto& tiles=dataStore->get<DataNs::TileList>("tiles");
        const auto& pairs=dataStore->get<std::vector<DataNs::PairMatches>>("matches");
        if (tiles.empty()) { result["error"]="position: no tiles"; return false; }
        std::map<int,std::size_t> index; std::vector<std::string> ids(tiles.size());
        const std::regex namePattern("_r([0-9]+)_c([0-9]+)\\.tiff?$",std::regex::icase);
        for (std::size_t i=0;i<tiles.size();++i) {
            index.emplace(tiles[i]->getId(),i);
            const std::string name=tiles[i]->getSource().filename().string(); std::smatch m;
            if (!std::regex_search(name,m,namePattern)) { result["error"]="position: tile filename must end in _r<row>_c<column>.tif[f]: "+name; return false; }
            ids[i]="r"+m[1].str()+"_c"+m[2].str();
        }
        struct Edge {std::size_t a,b; cv::Point2d d;}; std::vector<Edge> edges;
        for (const auto& pair:pairs) {
            const auto a=index.find(pair.tileIdA), b=index.find(pair.tileIdB);
            if (a==index.end()||b==index.end()) { result["error"]="position: match references unknown tile id"; return false; }
            const auto& m=pair.featureMatches;
            edges.push_back({a->second,b->second,{m.translationX,m.translationY}});
        }
        if (tiles.size()>1 && edges.empty()) { result["error"]="position: no measured neighbor pair"; return false; }
        std::vector<std::vector<std::pair<std::size_t,cv::Point2d>>> graph(tiles.size());
        for (const auto& e:edges) { graph[e.a].push_back({e.b,e.d}); graph[e.b].push_back({e.a,-e.d}); }
        std::vector<bool> seen(tiles.size(),false); std::vector<cv::Point2d> xy(tiles.size());
        for (std::size_t root=0;root<tiles.size();++root) if (!seen[root]) {
            if (root!=0) { result["error"]="position: tile grid is disconnected from the reference"; return false; }
            seen[root]=true; std::queue<std::size_t> q; q.push(root);
            while(!q.empty()) { auto a=q.front(); q.pop(); for(const auto& [b,d]:graph[a]) if(!seen[b]) {xy[b]=xy[a]+d; seen[b]=true;q.push(b);} }
        }
        std::vector<DataNs::TilePositionData> out; out.reserve(tiles.size());
        for(std::size_t i=0;i<tiles.size();++i) out.push_back({ids[i],xy[i],0.0});
        dataStore->replace("positions",std::move(out)); result["positions"]=tiles.size(); return true;
    } catch(const std::exception& e) { result["error"]=std::string("position: ")+e.what(); return false; }
}
}
