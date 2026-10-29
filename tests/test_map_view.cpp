#include "map_view.hpp"
#include "minimap_fixture.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void near(glm::dvec2 actual, glm::dvec2 expected, const char* message, double tolerance = 1e-5) {
    require(glm::length(actual-expected) < tolerance,message);
}
void click(viewer::MapView& view, glm::dvec2 screen) { view.move(screen); view.press(); view.release(); }
}
int main() {
    try {
        viewer::MapView view;
        view.setSource(viewer::findMinimapSource(minimapFixture()));
        view.setViewport({100,50},{600,400});
        require(!view.worldAt({150,200}),"Letterbox margin accepted as a map coordinate");
        near(*view.worldAt({200,50}),{-200,-150},"Northwest coordinate is wrong");
        near(*view.worldAt({600,450}),{200,150},"Southeast coordinate is wrong");
        near(view.screenAt({0,0}),{400,250},"Map center is wrong");
        view.zoomAt({450,200},4);
        near(*view.worldAt({450,200}),{50,-37.5},"Zoom did not preserve position under pointer");
        view.zoomAt({150,0},10);
        require(view.zoom() < 3,"Wheel outside map changed zoom");
        view.zoomAt({450,200},-32);
        require(view.zoom() == 1,"Zoom out did not stop at full image");
        near(view.imageRegion()->offset,{200,50},"Full-image fitting changed after zoom");

        view.beginMeasurement();
        click(view,{150,200});
        require(view.points().empty(),"Measurement started in an image margin");
        click(view,view.screenAt({-100,0}));
        click(view,view.screenAt({-100,0}));
        require(view.points().size()==1&&view.measuring(),"Single-point click finished or duplicated the start");
        click(view,view.screenAt({0,0}));
        view.move(view.screenAt({0,100}));
        near(*view.preview(),{0,100},"Preview uses wrong world coordinate");
        require(std::abs(view.distance()-100)<1e-5,"Preview changed committed distance");
        click(view,view.screenAt({0,100}));
        require(std::abs(view.distance()-200)<1e-5,"Multiple segment lengths are not summed in metres");
        view.undo();
        require(view.measuring()&&view.points().size()==2,"Right click did not undo just the last point");
        view.move(view.screenAt({0,0})+glm::dvec2(5,0));
        require(view.finishing()&&!view.preview(),"Last point lacks generous finish hit target");
        view.press(); view.release();
        require(!view.measuring()&&view.points().size()==2,"Last point click did not finish");
        view.undo(); view.cancel();
        require(view.points().size()==2,"Finished measurement lost on right click or close");

        // Completed vertices are editable; abandoning an edit restores it.
        view.move(view.screenAt({0,0})); view.press(); view.move(view.screenAt({0,100})); view.cancel();
        near(view.points().back(),{0,0},"Cancelled point edit was committed");
        view.move(view.screenAt({0,0})); view.press(); view.move(view.screenAt({0,100})); view.release();
        near(view.points().back(),{0,100},"Finished point could not be dragged");

        view.beginMeasurement(); click(view,view.screenAt({-100,0}));
        view.move({400,220}); view.press(); view.move({430,230}); view.move({400,220}); view.release();
        require(view.points().size()==1,"Pan out and back was mistaken for a point click");
        view.undo();
        require(view.points().empty()&&!view.measuring(),"Undoing start did not cancel measurement");
        view.beginMeasurement(); click(view,view.screenAt({0,0})); view.cancel();
        require(view.points().empty(),"Closing map retained unfinished points");

        // Panning really changes the world under the pointer, and image edges
        // constrain it even after an extreme drag. Resizing cancels a gesture.
        view.zoomAt({400,250},4);
        const auto before = *view.worldAt({400,250});
        view.move({400,250}); view.press(); view.move({440,250}); view.release();
        require(view.worldAt({400,250})->x < before.x,"Dragging right did not move the map right");
        view.move({400,250}); view.press(); view.move({100000,100000}); view.release();
        const auto region = view.imageRegion();
        require(region->uvOrigin.x>=-1e-10&&region->uvOrigin.y>=-1e-10&&
                region->uvOrigin.x+region->uvExtent.x<=1.000001&&region->uvOrigin.y+region->uvExtent.y<=1.000001,
                "Panning exposed coordinates outside imagery");
        view.beginMeasurement(); view.move({400,250}); view.press();
        view.setViewport({50,25},{300,200},.5); view.release();
        require(view.points().empty(),"Resizing turned a press into a point");
        view.cancel(); view.zoomAt({200,125},-32);

        const auto north = view.marker({0,0},-90), east = view.marker({0,0},0);
        near(north->direction,{0,-1},"North-facing triangle points the wrong way");
        near(east->direction,{1,0},"East-facing triangle points the wrong way");
        const auto remote = view.marker({10000,0},-90);
        require(remote&&remote->atEdge&&remote->direction.x>0,"Offscreen marker did not point toward player");

        auto transformed = minimapFixture();
        transformed.draws[0].transform = glm::translate(glm::mat4(1),{430,12,-810}) *
            glm::rotate(glm::mat4(1),glm::radians(90.f),{0,1,0}) * glm::scale(glm::mat4(1),{-2,1,3});
        view.setSource(viewer::findMinimapSource(transformed));
        view.setViewport({0,0},{400,400});
        // The authored rotation is stored in a float32 scene-node matrix.
        near(*view.worldAt({0,0}),{-20,-1210},"Reflected/rotated image mapping lost node transform",1e-3);
        near(*view.worldAt({400,400}),{880,-410},"Transformed image southeast is wrong",1e-3);
        const auto transformedNorth = view.marker({430,-810},-90);
        near(transformedNorth->direction,{-1,0},"Marker heading ignored transformed imagery");
        view.beginMeasurement(glm::dvec2(430,-810));
        require(view.points().size()==1,"Start at player did not place start");
        view.setSource({});
        require(!view.imageRegion()&&!view.worldAt({200,200})&&!view.marker({0,0},0)&&view.points().empty(),"Source removal left stale map state");
        std::cout << "Map view checks passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
