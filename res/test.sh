for src in *.easm; do
    base="${src%.easm}"
    file="${base}.evm"
    payload_file="${base}.bin"
    input_file="${base}.in"
    output_file="${base}.out"

    echo "Running $src"

    python compiler.py "$src" "$file"

    if [[ -f "$input_file" ]]; then
        ./test.elf "$file" "$payload_file" < "$input_file" > "$output_file" 2>&1
    else
        ./test.elf "$file" "$payload_file" > "$output_file" 2>&1
    fi

done