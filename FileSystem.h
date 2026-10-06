#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include "FileNode.h"

#include <cstddef>
#include <functional>
#include <string>

// FileSystem 公共错误码。
// GUI 可以根据它决定弹出什么提示；搜索、持久化等模块通常只需要判断返回值。
enum class FileSystemError {
    NONE,
    INVALID_NODE,
    INVALID_TARGET,
    INVALID_NAME,
    NAME_CONFLICT,
    NOT_DIRECTORY,
    NOT_FILE,
    ROOT_OPERATION,
    INVALID_MOVE,
    NOT_FOUND,
    NOT_IN_RECYCLE_BIN,
    INVALID_METADATA
};

class Operation;

class FileSystem
{
public:
    using NodeVisitor = std::function<void(FileNode *)>;
    using RecycleVisitor = std::function<void(FileNode *, const std::string &original_path)>;

    FileSystem();
    ~FileSystem();

    FileSystem(const FileSystem &) = delete;
    FileSystem &operator=(const FileSystem &) = delete;

    // =========================
    // 创建
    // =========================

    // 在指定目录下创建普通模拟文件。
    // 初始大小由 content.size() 决定。
    // 成功返回新节点；失败返回 nullptr，并可通过 get_last_error() 获取原因。
    FileNode *create_file(FileNode *parent,
                          const std::string &name,
                          const std::string &content = "");

    // 在指定目录下创建文件夹。
    FileNode *create_folder(FileNode *parent, const std::string &name);

    // =========================
    // 基本文件操作
    // =========================

    // 查看目录
    FileNode *get_current_directory() const;

    bool change_directory(FileNode *directory);

    bool change_directory_by_path(const std::string &path);

    // 普通删除：从活动树摘除，放入回收站，不立即释放内存。
    bool delete_node(FileNode *node);

    // 从回收站恢复到指定活动目录。
    bool restore_node(FileNode *node, FileNode *target_parent);

    // 永久删除：只接受回收站中的节点，并递归释放整棵子树。
    bool permanent_delete(FileNode *node);

    // 清空回收站并永久释放其中所有节点。
    void clear_recycle_bin();

    bool rename_node(FileNode *node, const std::string &new_name);

    bool move_node(FileNode *node, FileNode *target_parent);

    // 置顶
    bool set_pinned(FileNode* node, bool pinned);
    bool is_pinned(FileNode* node) const;


    // 递归深拷贝整个子树。
    FileNode *copy_node(FileNode *node, FileNode *target_parent);

    // =========================
    // 查找
    // =========================

    FileNode *find_child(FileNode *parent, const std::string &name) const;

    // 路径规则：
    // "/"                       -> 根目录
    // "/docs/a.txt"             -> 从根目录开始的绝对路径
    // "docs/a.txt"              -> 从当前目录开始的相对路径
    // "."                       -> 当前目录
    // ".."                      -> 当前目录的父目录；根目录继续停留在根目录
    FileNode *find_by_path(const std::string &path) const;

    // 遍历 / 搜索支持
    // 深度优先遍历 start_node 子树，包含 start_node 自身。
    // 搜索模块不需要知道孩子-兄弟指针的内部实现。
    void traverse(FileNode *start_node, const NodeVisitor &visitor) const;


    // =========================
    // 文件内容
    // =========================

    // 只有普通文件允许设置内容。
    // 会同步更新大小与修改时间。
    bool set_content(FileNode *file, const std::string &content);

    const std::string &get_content(FileNode *file) const;

    // =========================
    // 属性 / 持久化
    // =========================

    // 用于持久化模块恢复节点属性。
    // size 必须 >= 0；空时间字符串会被替换为当前时间。
    bool set_metadata(FileNode *node,
                      long long size,
                      const std::string &created_time,
                      const std::string &modified_time);


    // =========================
    // 路径与根目录
    // =========================

    // 活动树中的节点返回其当前完整路径；回收站节点应使用
    // get_recycle_original_path() 获取删除前路径。
    std::string get_path(FileNode *node) const;

    FileNode *get_root() const;


    // =========================
    // 回收站查询
    // =========================

    bool is_in_recycle_bin(FileNode *node) const;

    std::string get_recycle_original_path(FileNode *node) const;

    std::size_t get_recycle_count() const;

    void for_each_recycle_item(const RecycleVisitor &visitor) const;


    // =========================
    // 合法性检查
    // =========================

    bool has_name_conflict(FileNode *parent,
                           const std::string &name,
                           FileNode *ignore_node = nullptr) const;

    bool can_move(FileNode *node, FileNode *target_parent) const;


    // =========================
    // 错误信息
    // =========================

    FileSystemError get_last_error() const;


    // =========================
    // 撤销
    // =========================

    // operation 由上层撤销模块从 OperationStack 中取出。
    // FileSystem 负责执行对应的逆操作。
    bool undo(const Operation &operation);


    // =========================
    // 重置整个虚拟文件系统
    // =========================

    // 清空正常文件树和回收站，保留根节点。
    void reset();

private:
    struct RecycleEntry
    {
        FileNode *node;
        std::string original_path;
        RecycleEntry *next;

        RecycleEntry(FileNode *node_value, const std::string &path_value, RecycleEntry *next_value)
            : node(node_value)
            , original_path(path_value)
            , next(next_value)
        {}
    };

    FileNode *root_;
    FileNode *current_directory_;
    RecycleEntry *recycle_head_;
    std::size_t recycle_count_;
    FileSystemError last_error_;

    static bool is_valid_name(const std::string &name);
    static std::string now_string();

    void set_error(FileSystemError error);

    bool owns_active_node(FileNode *node) const;
    bool is_descendant(FileNode *node, FileNode *possible_ancestor) const;

    void append_child(FileNode *parent, FileNode *node);
    bool detach_node(FileNode *node);

    FileNode *clone_subtree(FileNode *source, FileNode *new_parent);

    void destroy_subtree(FileNode *node);

    RecycleEntry *find_recycle_entry(FileNode *node) const;
    void remove_recycle_entry(RecycleEntry *entry);

    void traverse_recursive(FileNode *node, const NodeVisitor &visitor) const;

    long long next_pin_order_;
};

#endif // FILESYSTEM_H
