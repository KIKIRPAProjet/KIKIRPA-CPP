#include "steps/assembly_step.h"
#include "algorithms/egalisation/Egalisation.h"
#include "core/config/pipeline_config.h"
#include "core/data/tile.h"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <cmath>
#include <map>

namespace StepsNs {
AssemblyStepClass::AssemblyStepClass(){ConfigNs::StepConfig c;c.name="assemble";c.inputs={{"Tiles","tiles"},{"Positions","positions"},{"TileCorrections","corrections"},{"Territories","territories"}};c.outputs={{"Mosaic","mosaic"}};configJson=c;}
bool AssemblyStepClass::execute(){
 result=Json::object();
 try{
  const auto& tiles=dataStore->get<DataNs::TileList>("tiles");const auto& positions=dataStore->get<std::vector<DataNs::TilePositionData>>("positions");const auto& corrections=dataStore->get<DataNs::TileCorrections>("corrections");const auto& territories=dataStore->get<std::vector<DataNs::TileTerritory>>("territories");
  if(tiles.empty()||positions.size()!=tiles.size()||territories.size()!=tiles.size()){result["error"]="assemble: tiles, positions and territories must have matching nonzero sizes";return false;}
  std::map<int,DataNs::TileCorrection> correctionById;for(const auto& c:corrections)correctionById[c.tileId]=c;
  double minX=positions.front().position.x,minY=positions.front().position.y;int width=0,height=0;std::vector<cv::Mat> images;images.reserve(tiles.size());
  for(std::size_t i=0;i<tiles.size();++i){minX=std::min(minX,positions[i].position.x);minY=std::min(minY,positions[i].position.y);cv::Mat im=cv::imread(tiles[i]->getSource().string(),cv::IMREAD_UNCHANGED);if(im.empty()){result["error"]="assemble: cannot read "+tiles[i]->getSource().string();return false;}auto c=correctionById.find(tiles[i]->getId());if(c!=correctionById.end()){stitching::egalisation::Resultat r;r.gainsB={c->second.gain};r.c=c->second.fieldStrength;im=stitching::egalisation::Egalisation::corriger(im,r,0,stitching::egalisation::Methode::GainEtChamp);}width=std::max(width,static_cast<int>(std::ceil(positions[i].position.x-minX))+im.cols);height=std::max(height,static_cast<int>(std::ceil(positions[i].position.y-minY))+im.rows);images.push_back(std::move(im));}
  if(width<=0||height<=0){result["error"]="assemble: computed mosaic dimensions are empty";return false;}
  cv::Mat canvas=cv::Mat::zeros(height,width,images.front().type());
  for(std::size_t i=0;i<tiles.size();++i){int x=cvRound(positions[i].position.x-minX),y=cvRound(positions[i].position.y-minY);cv::Rect roi(x,y,images[i].cols,images[i].rows);if((roi&cv::Rect(0,0,width,height))!=roi){result["error"]="assemble: tile lies outside computed canvas";return false;}cv::Mat mask=cv::Mat::zeros(images[i].size(),CV_8U);if(i<territories.size()){for(std::size_t p=0;p<territories[i].polygons.size();++p){cv::fillPoly(mask,std::vector<std::vector<cv::Point>>{territories[i].polygons[p]},cv::Scalar(255));if(p<territories[i].holes.size())for(const auto& hole:territories[i].holes[p])cv::fillPoly(mask,std::vector<std::vector<cv::Point>>{hole},cv::Scalar(0));}}if(cv::countNonZero(mask)==0){result["error"]="assemble: tile territory is empty for index "+std::to_string(i);return false;}images[i].copyTo(canvas(roi),mask);}
  DataNs::MosaicData mosaic{canvas};dataStore->replace("mosaic",std::move(mosaic));result["width"]=width;result["height"]=height;return true;
 }catch(const std::exception& e){result["error"]=std::string("assemble: ")+e.what();return false;}
}
}
