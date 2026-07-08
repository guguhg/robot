#pragma once
#include <mutex>
#include <cstdint>
#include <map>
#include <vector>

namespace drivers
{
    class Motors
    {
    public:
        /**
         * @brief 获取指定ID电机的速度
         * 
         * @param id 电机ID
         * @return float 转速rs +为正转 -为反转 0为停止    
         */
        static float getMotorSpeed_rs(const uint8_t id);

        /**
         * @brief 获取所有电机的速度，以字典形式返回
         * 
         * @return std::map<uint8_t, float> id:rs
         */
        static std::map<uint8_t, float> getMotorSpeed_rs();

        /**
         * @brief 控制单个电机的转速
         * 
         * @param id 电机ID
         * @param speed_rs 转速，范围具体见config.yaml
         * @return true 发送指令成功
         * @return false 发送指令失败
         */
        static bool ctrlMotorSpeed_rs(const uint8_t id, const float speed_rs);

        /**
         * @brief 控制多个电机的转速
         * 
         * @param mt_op 字典 id:rs
         * @return true 发送指令成功
         * @return false 发送指令失败
         */
        static bool ctrlMotorSpeed_rs(const std::map<uint8_t, float> &mt_op);

        /**
         * @brief 停下指定ID的电机
         * 
         * @param motor_id  电机ID
         * @return true 发送指令成功
         * @return false 发送指令失败
         */
        static bool ctrlMotorStop(const uint8_t motor_id);

        /**
         * @brief 停下多个电机
         * 
         * @param mt_op 电机ID组
         * @return true 发送指令成功
         * @return false 发送指令失败
         */
        static bool ctrlMotorStop(const std::vector<uint8_t> &mt_op);
        
        //禁止拷贝
        Motors(const Motors &) = delete;
        Motors &operator=(const Motors &) = delete;

    private:
        explicit Motors();//构造函数,外部构造要求显式调用
        ~Motors() = default;         
        static Motors& getInstance();// 获取全局静态私有单例句柄

        //私有实现
        float getMotorSpeed_rs_private(const uint8_t id);
        std::map<uint8_t, float> getMotorSpeed_rs_private();
        bool ctrlMotorSpeed_rs_private(const uint8_t id, const float speed_rs);
        bool ctrlMotorSpeed_rs_private(const std::map<uint8_t, float> &mt_op);
        bool ctrlMotorStop_private(const uint8_t motor_id);
        bool ctrlMotorStop_private(const std::vector<uint8_t> &mt_op);
        
        std::map<uint8_t, float> motor_od_;//电机数据存储字典  
        uint8_t motor_count_;//电机个数
        float max_speed_rs_;//正转最大转速rs,停止为0
        float min_speed_rs_;//反转最大转速rs,停止为0
        
        std::mutex mutex_;//共享资源互斥锁
    };
}