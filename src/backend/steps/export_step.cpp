#include "steps/export_step.h"
#include "core/config/pipeline_config.h"
#include <opencv2/imgcodecs.hpp>
#include <filesystem>
namespace StepsNs {
ExportStepClass::ExportStepClass(){ConfigNs::StepConfig c;c.name="export";c.parameters={{"string","path","output/mosaic.tif"}};c.inputs={{"Mosaic","mosaic"}};c.outputs={{"ExportPath","exportPath"}};configJson=c;}
bool ExportStepClass::execute(){result=Json::object();try{const auto& mosaic=dataStore->get<DataNs::MosaicData>("mosaic");const std::filesystem::path path=getParameter("path").get<std::string>();if(mosaic.pixels.empty()){result["error"]="export: mosaic is empty";return false;}if(path.has_parent_path())std::filesystem::create_directories(path.parent_path());if(!cv::imwrite(path.string(),mosaic.pixels)){result["error"]="export: image encoder refused "+path.string();return false;}dataStore->replace("exportPath",path.string());result["path"]=path.string();return true;}catch(const std::exception& e){result["error"]=std::string("export: ")+e.what();return false;}}
}
