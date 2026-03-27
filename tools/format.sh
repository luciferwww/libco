#!/bin/bash
# format.sh - 代码格式化脚本
# 使用 clang-format 格式化所有源代码文件

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# 检查 clang-format 是否安装
if ! command -v clang-format &> /dev/null; then
    echo -e "${RED}Error: clang-format not found!${NC}"
    echo -e "${YELLOW}Please install clang-format:${NC}"
    echo -e "${YELLOW}  - Ubuntu/Debian: sudo apt-get install clang-format${NC}"
    echo -e "${YELLOW}  - macOS: brew install clang-format${NC}"
    exit 1
fi

echo -e "${CYAN}Using: $(command -v clang-format)${NC}"
echo -e "${CYAN}Version: $(clang-format --version)${NC}"

# 参数解析
CHECK_ONLY=false
if [ "$1" == "--check" ]; then
    CHECK_ONLY=true
fi

# 要格式化的目录
DIRECTORIES=(
    "libco/src"
    "libco/include"
    "libcoxx/src"
    "libcoxx/include"
    "tests"
    "examples"
)

# 文件扩展名
EXTENSIONS=("c" "h" "cpp" "hpp" "cc" "hh")

TOTAL_FILES=0
FORMATTED_FILES=0
declare -a BAD_FORMAT_FILES

# 切换到项目根目录
cd "$(dirname "$0")/.."

for dir in "${DIRECTORIES[@]}"; do
    if [ ! -d "$dir" ]; then
        echo -e "${YELLOW}Skip: $dir (not exists)${NC}"
        continue
    fi
    
    echo -e "\n${GREEN}Processing: $dir${NC}"
    
    for ext in "${EXTENSIONS[@]}"; do
        while IFS= read -r -d '' file; do
            TOTAL_FILES=$((TOTAL_FILES + 1))
            RELATIVE_PATH="${file#./}"
            
            if [ "$CHECK_ONLY" == true ]; then
                # 只检查格式
                if ! clang-format --dry-run -Werror "$file" 2>/dev/null; then
                    FORMATTED_FILES=$((FORMATTED_FILES + 1))
                    BAD_FORMAT_FILES+=("$RELATIVE_PATH")
                    echo -e "  ${RED}✗ $RELATIVE_PATH${NC}"
                else
                    echo -e "  ${GREEN}✓ $RELATIVE_PATH${NC}"
                fi
            else
                # 格式化文件
                if clang-format -i "$file"; then
                    FORMATTED_FILES=$((FORMATTED_FILES + 1))
                    echo -e "  ${CYAN}✓ $RELATIVE_PATH${NC}"
                else
                    echo -e "  ${RED}✗ $RELATIVE_PATH (failed)${NC}"
                fi
            fi
        done < <(find "$dir" -type f -name "*.$ext" -print0 2>/dev/null)
    done
done

# 输出统计
echo -e "\n========================================"
if [ "$CHECK_ONLY" == true ]; then
    echo -e "${CYAN}Format Check Complete${NC}"
    echo -e "Total files: $TOTAL_FILES"
    
    if [ $FORMATTED_FILES -eq 0 ]; then
        echo -e "${GREEN}Bad format: $FORMATTED_FILES${NC}"
        echo -e "${GREEN}All files are properly formatted! ✓${NC}"
        exit 0
    else
        echo -e "${RED}Bad format: $FORMATTED_FILES${NC}"
        echo -e "\n${YELLOW}Files need formatting:${NC}"
        for file in "${BAD_FORMAT_FILES[@]}"; do
            echo -e "  - $file"
        done
        echo -e "\n${YELLOW}Run './tools/format.sh' to fix${NC}"
        exit 1
    fi
else
    echo -e "${CYAN}Format Complete${NC}"
    echo -e "Total files: $TOTAL_FILES"
    echo -e "${GREEN}Formatted: $FORMATTED_FILES${NC}"
fi

exit 0
