#include "../FileSystem.h"

#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

namespace {
void expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        std::exit(1);
    }
}
}

int main()
{
    FileSystem fs;
    FileNode *root = fs.get_root();
    FileNode *source = fs.create_folder(root, "source");
    FileNode *target = fs.create_folder(root, "target");
    FileNode *move_target = fs.create_folder(root, "move_target");
    FileNode *nested = fs.create_folder(source, "nested");
    FileNode *file = fs.create_file(nested, "a.txt", "hello");

    expect(source && target && move_target && nested && file, "test tree creation");
    const std::string source_modified = file->get_modified_time();

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    FileNode *file_copy = fs.copy_node(file, target);
    expect(file_copy != nullptr, "copy file succeeds");
    expect(file_copy->get_name() == "a.txt", "copied file keeps name");
    expect(file_copy->get_content() == "hello", "copied file keeps content");
    expect(file_copy->get_size() == file->get_size(), "copied file keeps size");
    expect(file_copy->get_modified_time() == source_modified,
           "copied file keeps source modification time");
    expect(file_copy->get_created_time() != file->get_created_time(),
           "copied file gets a fresh creation time");

    expect(fs.set_content(file_copy, "changed"), "editing copied file succeeds");
    expect(file->get_content() == "hello", "editing copy does not change source");

    FileNode *folder_copy = fs.copy_node(source, target);
    expect(folder_copy != nullptr, "copy folder succeeds");
    FileNode *nested_copy = fs.find_child(folder_copy, "nested");
    FileNode *deep_copy = nested_copy ? fs.find_child(nested_copy, "a.txt") : nullptr;
    expect(nested_copy != nullptr && deep_copy != nullptr, "recursive copy keeps subtree");
    expect(deep_copy->get_content() == "hello", "recursive copy keeps deep content");

    expect(fs.copy_node(source, nested) == nullptr,
           "copying a folder into its own descendant is rejected");
    expect(fs.get_last_error() == FileSystemError::INVALID_MOVE,
           "invalid recursive copy reports INVALID_MOVE");

    expect(fs.move_node(file, move_target), "move file succeeds");
    expect(file->get_parent() == move_target, "moved file has new parent");
    expect(fs.get_path(file) == "/move_target/a.txt", "moved file path updates");

    expect(!fs.move_node(source, nested), "moving a folder into its descendant is rejected");
    expect(fs.get_last_error() == FileSystemError::INVALID_MOVE,
           "invalid folder move reports INVALID_MOVE");

    expect(fs.delete_node(source), "delete source folder to recycle bin succeeds");
    expect(fs.is_in_recycle_bin(source), "deleted folder enters recycle bin");
    expect(fs.permanent_delete(source), "permanent delete succeeds");
    expect(!fs.is_in_recycle_bin(source), "permanently deleted folder leaves recycle bin");

    std::cout << "[PASS] copy/move/recursive core operations" << std::endl;
    return 0;
}
