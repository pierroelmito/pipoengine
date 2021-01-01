
pipo: *.cpp *.h shader_default.inl
	g++ -o pipo main.cpp pipoengine.cpp -lSDL2 -lGLEW -lGL -llua -std=c++17 -Wall -O0 -g

shader_default.inl : default.shader
	sokol-shdc --input $< --output $@ --slang glsl330

clean:
	rm -f pipo

run: pipo
	./pipo
