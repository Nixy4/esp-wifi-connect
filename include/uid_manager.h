#ifndef UID_MANAGER_H
#define UID_MANAGER_H

#include <string>

class UidManager {
public:
    static UidManager& GetInstance() {
        static UidManager instance;
        return instance;
    }

    void SetUid(const std::string& uid);
    std::string GetUid() const { return uid_; }
    bool HasUid() const { return !uid_.empty(); }
    void Clear();

private:
    UidManager();
    ~UidManager();

    void LoadFromNvs();
    void SaveToNvs();

    std::string uid_;
};

#endif // UID_MANAGER_H
