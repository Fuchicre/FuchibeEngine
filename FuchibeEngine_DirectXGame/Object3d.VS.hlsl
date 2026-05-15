struct VertexShadeOutput{
    float32_t4 position : SV_POSITION;
};

struct VertexShaderInput{
    float32_t4 position : POSITION0;
};

VertexShadeOutput main(VertexShaderInput input){
    VertexShadeOutput output;
    output.position = input.position;
    return output;
};