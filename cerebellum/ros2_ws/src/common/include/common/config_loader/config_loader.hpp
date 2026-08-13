#pragma once

#include <string>
#include <yaml-cpp/yaml.h>
#include <stdexcept>

namespace common
{

    /**
     * @brief 通用配置加载器,每个加载的程序都有独立的实例
     *
     * 用法:
     *   YAML::Node config = common::ConfigLoader::loadDefault();
     *   std::string port = config["drivers"]["controller_board"]["serial"]["port"].as<std::string>();
     */
    class ConfigLoader
    {
    public:
        /**
         * @brief 加载默认全局配置文件
         * @return YAML 根节点
         * @throws std::runtime_error 加载失败
         */
        static YAML::Node loadDefault();

        /**
         * @brief 加载指定路径文件
         * @param file_path 文件路径
         * @return YAML 根节点
         * @throws std::runtime_error 加载失败
         */
        static YAML::Node load(const std::string &file_path);

        /**
         * @brief 打印配置内容（调试用）
         * @param node 节点实例
         * @param title 打印标题
         */
        static void dump(const YAML::Node &node, const std::string &title = "Config");

    private:
        static std::string getDefaultPath();//获取可用全局配置文件路径
        static std::vector<std::string> getDefaultPaths();//载入所有可能全局配置文件路径
    };

} // namespace common