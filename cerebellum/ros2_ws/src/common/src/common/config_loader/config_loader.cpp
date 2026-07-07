#include "common/config_loader/config_loader.hpp"
#include <iostream>
#include <fstream>
#include <cstdlib>

namespace common
{

    /**
     * @brief 获取默认的全局配置文件,优先从ROBOT_CONFIG_PATH环境变量中获取,再加入有可能的配置文件路径
     *
     * @return 路径列表
     */
    std::vector<std::string> ConfigLoader::getDefaultPaths()
    {
        std::vector<std::string> paths;
        const char *env_path = std::getenv("ROBOT_CONFIG_PATH");
        if (env_path != nullptr)
        {
            paths.push_back(std::string(env_path));
        }
        paths.push_back("/ros2_ws/src/common/config/config.yaml");
        paths.push_back("/ros2_ws/install/common/share/common/config/config.yaml");
        paths.push_back("./config/config.yaml");
        paths.push_back("config/config.yaml");
        return paths;
    }

    /**
     * @brief 找到第一个能够打开的全局配置文件
     *
     * @return 路径
     */
    std::string ConfigLoader::getDefaultPath()
    {
        for (const auto &path : getDefaultPaths())
        {
            if (std::ifstream(path).good())
            {
                return path;
            }
        }
        return getDefaultPaths().front();
    }

    /**
     * @brief 载入全局配置文件
     *
     * @return YAML:根节点
     */
    YAML::Node ConfigLoader::loadDefault()
    {
        return load(getDefaultPath());
    }

    /**
     * @brief 载入指定路径的yaml文件
     *
     * @param file_path 路径
     * @return YAML:根节点
     */
    YAML::Node ConfigLoader::load(const std::string &file_path)
    {
        if (!std::ifstream(file_path).good())
        { // 判断文件能否打开
            std::cerr << "[Config] File not found: " << file_path << std::endl;
            throw std::runtime_error("[Config] File not found: " + file_path);
        }

        try
        {
            YAML::Node root = YAML::LoadFile(file_path);
            std::cout << "[Config] Loaded from: " << file_path << std::endl;
            return root;
        }
        catch (const YAML::Exception &e)
        {
            std::cerr << "[Config] YAML parse error: " << e.what() << std::endl;
            throw std::runtime_error("[Config] YAML parse error: " + std::string(e.what()));
        }
    }

    /**
     * @brief 打印配置内容（调试用）
     * @param node 节点实例
     * @param title 打印标题
     */
    void ConfigLoader::dump(const YAML::Node &node, const std::string &title)
    {
        std::cout << "========== " << title << " ==========" << std::endl;
        std::cout << node << std::endl;
        std::cout << "====================================" << std::endl;
    }

} // namespace common