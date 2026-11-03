#include "steps/equalization_step.h"
#include "algorithms/egalisation/Egalisation.h"
#include "core/config/pipeline_config.h"
#include "core/data/tile.h"
#include <opencv2/imgcodecs.hpp>
#include <map>

namespace StepsNs {
EqualizationStepClass::EqualizationStepClass() { ConfigNs::StepConfig c; c.name="equalization"; c.inputs={{"Tiles","tiles"},{"Positions","positions"}}; c.outputs={{"TileCorrections","corrections"}}; configJson=c; }
bool EqualizationStepClass::execute() {
 result=Json::object();
 try {
  const auto& tiles=dataStore->get<DataNs::TileList>("tiles"); const auto& positions=dataStore->get<std::vector<DataNs::TilePositionData>>("positions");
  if(tiles.size()<2||positions.size()!=tiles.size()){result["error"]="equalization: expected at least two tiles and one position per tile";return false;}
  std::map<std::string,std::size_t> posIndex; std::vector<cv::Point2d> xy; xy.reserve(positions.size());
  for(std::size_t i=0;i<positions.size();++i){posIndex.emplace(positions[i].tileId,i);xy.push_back(positions[i].position);}
  std::map<int,std::size_t> tileById; for(std::size_t i=0;i<tiles.size();++i) tileById.emplace(tiles[i]->getId(),i);
  std::vector<stitching::egalisation::TuileReduite> reduced; reduced.reserve(tiles.size()); stitching::egalisation::Egalisation algorithm;
  for(const auto& tile:tiles){cv::Mat image=cv::imread(tile->getSource().string(),cv::IMREAD_UNCHANGED);if(image.empty()){result["error"]="equalization: cannot read "+tile->getSource().string();return false;}reduced.push_back(algorithm.reduire(image));}
  std::vector<std::pair<int,int>> neighbors;
  for(std::size_t i=0;i<tiles.size();++i) for(const auto& n:{tiles[i]->getRight(),tiles[i]->getBottom()}) if(n){auto it=tileById.find(n->getId());if(it!=tileById.end()) neighbors.emplace_back(static_cast<int>(i),static_cast<int>(it->second));}
  std::vector<stitching::egalisation::PaireVoisine> pairs;
  for(const auto& p:neighbors) pairs.push_back({p.first,p.second,xy.at(p.second)-xy.at(p.first)});
  auto estimate=algorithm.estimer(reduced,pairs);
  if(!estimate){result["error"]="equalization: "+algorithm.raisonEchec();return false;}
  DataNs::TileCorrections corrections; corrections.reserve(tiles.size());
  for(std::size_t i=0;i<tiles.size();++i) corrections.push_back({tiles[i]->getId(),estimate->gainsB.at(i),estimate->c});
  dataStore->replace("corrections",std::move(corrections)); result["tiles"]=tiles.size(); result["samples"]=estimate->nbEchantillons; return true;
 } catch(const std::exception& e){result["error"]=std::string("equalization: ")+e.what();return false;}
}
}
