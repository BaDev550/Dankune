# Dankune

Simple 2D game engine to make a dungeon game engine is easy to understand
because it doesnt have anthing :p

Everything about rendering is inside of (Engine/RenderContext, Engine/SceneRenderer)
This was a lazynes project overall it is not the best but not the worst but dont do it like this
Dont put everything inside rendercontext

<img width="1920" height="1009" alt="Vulkan Game 6_17_2026 2_23_49 AM" src="https://github.com/user-attachments/assets/73e3a9af-7872-4720-84ca-168d58bea220" />

Dont forget when you build the project you need to copy resources file from game folder to the "out/build/64-debug/game" or smt
This problaby dosnt works on linux I didnt tested and I dont want to :)

and btw it has some sync issues with vulkan when u destroy the buffer of a chunk it deletes instanty and command buffer is still using it we dont wait for our signal and then booom app goes crazy deletion
of buffer shoud be affter we wait for semaphores but I'm not gonna do it because it works and just gives error and this is not my main project and I dont wanna go and do sync track objects the right way
bc its gonna cost me a lots of time and I have a exam coming up... :/
