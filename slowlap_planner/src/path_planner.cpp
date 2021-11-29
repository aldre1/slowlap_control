/**
 * This handles the path planning algorithm
 * node.cpp passes cone and car info through the function update(...)
 * new cones are copied, then sorted by colour, and by correct order in race track (not necessarily by distance)
 * Path points are then generated by taking the mid point between 2 opposite cones 
 * then pass back path information to node.cpp
 * 
 * for future improvement: generate velocity reference as well
 * 
 * author: Aldrei Recamadas (MURauto21)
 **/

#include "path_planner.h"

//constructor
PathPlanner::PathPlanner(float car_x, float car_y, std::vector<Cone> &cones, bool const_velocity, float v_max, float v_const, float max_f_gain, std::vector<PathPoint>&markers)
    : const_velocity(const_velocity), v_max(v_max), v_const(v_const), f_gain(max_f_gain), car_pos(PathPoint(car_x,car_y)),init_pos(PathPoint(car_x,car_y))
{
	//set capacity of vectors
	raw_cones.reserve(500);
	left_unsorted.reserve(50);
	right_unsorted.reserve(50);
	left_cones.reserve(250);
	right_cones.reserve(250);
	centre_points.reserve(300);
	cenPoints_temp1.reserve(50);
	cenPoints_temp2.reserve(50);
	rejected_points.reserve(300);
	thisSide_cone.reserve(150);
	oppSide_cone.reserve(150);
	oppSide_cone2.reserve(50);
	future_cones.reserve(50);
	timing_cones.reserve(10);

	addCones(cones);								// add new cones to raw cones
	centre_points.emplace_back(car_x,car_y);		// add the car's initial position to centre points  
	addFirstCentrePoints();								// add centre points from sorted cones
	centralizeTimingCones();						// get mid point of orange cones
	if (timingCalc)
		sortPathPoints(centre_points,init_pos);
	resetTempConeVectors();							// Clear pointers and reset l/right_unsorted
	if (DEBUG) std::cout << "[PLANNER] initial path points size : " << centre_points.size() <<std::endl; //this should give 3 under normal circumstances
}

// takes car and cone infor from node.cpp then update pathpoints to be passed back to node.cpp
void PathPlanner::update(std::vector<Cone> &new_cones, const float car_x, const float car_y,
						 std::vector<PathPoint> &Path,
						 std::vector<Cone> &Left,std::vector<Cone> &Right, std::vector<PathPoint> &markers, bool&plannerComp)
{
	if (complete) // if race track is complete
	{	returnResult(Path,Left,Right,markers);
		plannerComp = true;
	}
	else
	{
		car_pos = PathPoint(car_x,car_y); //update car's position
		
		if (left_start_zone)
		{
			// join track if feasible
			if (joinFeasible(car_x, car_y))
			{
				ROS_INFO_STREAM("[PLANNER] Race track almost complete");
				centre_points.push_back(init_pos);
				reached_end_zone = true;
				complete = true;

			}
		}
		else
		{
			if (calcDist(init_pos, car_pos) > 15) //if greater than 15m away
				left_start_zone = true;
		}
		
		if (!reached_end_zone)
		{
			addCones(new_cones);
			updateCentrePoints();
			if (newConesToSort)
			{
				if (!timingCalc && !left_start_zone) // if orange cones not yet passedBy
				{
					centralizeTimingCones();
					sortPathPoints(centre_points,init_pos);
				}
				if (!left_cones.empty() && !right_cones.empty())
				{
					sortAndPushCone(left_unsorted);
					sortAndPushCone(right_unsorted);	
					addCentrePoints();
				}
				
			}
		}

		returnResult(Path, Left, Right,markers);	
		
		resetTempConeVectors();
	}
}

//function to sort pathpoints by distance to a reference point
void PathPlanner::sortPathPoints(std::vector<PathPoint>&cenPoin,PathPoint&refPoint)
{
	
	for (auto &p:cenPoin)
	{
		p.dist = calcDist(p,refPoint);
	}
	sort(cenPoin.begin(),cenPoin.end(),comparePointDist);
}

// checks whether track is (almost) finished
bool PathPlanner::joinFeasible(const float &car_x, const float &car_y)
{
	float dist = calcDist(centre_points.back(), init_pos);
	if (DEBUG) std::cout<<"[PLANNER] Distance of latest path point to finish line: "<<(dist+6)<<std::endl; //start/finish line is 6m in fron to init (rules)
	if ( (dist) < 5 || (calcDist(car_pos,left_cones.front()->position)<CERTAIN_RANGE)) //if less than 5 or 2 meters (magic number), should define in h file
	{
		float angle = calcRelativeAngle(centre_points.front(), centre_points.back()) - calcRelativeAngle(centre_points.back(), *(centre_points.end() - 2));
		// float angle = calcAngle(*(centre_points.end() - 2), centre_points.back(), centre_points.front());
		std::cout << angle << std::endl;
		if (abs(angle) < MAX_PATH_ANGLE1 || abs(angle)> MAX_PATH_ANGLE2)
		{
			return true;
		}
	}
	else
		return false;
}

// sets the vectors to be returned to node.cpp
void PathPlanner::returnResult(std::vector<PathPoint> &cp,
								std::vector<Cone>&Left, std::vector<Cone>&Right,std::vector<PathPoint>&markers)
{
	int j=0;
	if (DEBUG) std::cout<<"[PLANNER] sent path points: "<<centre_points.size()<<std::endl;
	for (auto &e: centre_points)
	{
		cp.push_back(e);
		j++;
		if ((e.cone1 != NULL)&&(centre_points.size()-j<10)) //to show only 10 markers
		{
			markers.push_back(e.cone1->position);
			markers.back().accepted = true;
			markers.push_back(e.cone2->position);
			markers.back().accepted = true;
		}
	}

	// for rviz visualisation purposes
	if (!rejected_points.empty())
	{
		rejectCount++;
		for (auto &r: rejected_points)
		{
			markers.push_back(r.cone1->position);
			markers.back().accepted = false;
			markers.push_back(r.cone2->position);
			markers.back().accepted = false;
		}
	}

	// push sorted cones
	for (auto lc:left_cones)
	{
		Left.push_back(*lc);
	}

	for (auto rc:right_cones)
	{
		Right.push_back(*rc);
	}
}

// calculates the angle difference. used for cone sorting
float PathPlanner::calcAngle(const PathPoint &A, const PathPoint &B, const PathPoint &C)
{
	float cb_x = C.x - B.x;
	float cb_y = C.y - B.y;
	float ca_x = B.x - A.x;
	float ca_y = B.y - A.y;

	float angle = calcRelativeAngle(C,B) - calcRelativeAngle(B,A);

	return angle;
}

// calculates angle between 2 points (global frame)
float PathPlanner::calcRelativeAngle(const PathPoint &p1, const PathPoint &p2)
{
	float angle = (atan2(p2.y - p1.y, p2.x - p1.x))* 180 / M_PI;
	return angle;
}

// generates path points by getting the mid point between 2 cones.
// points can be accepted or rejected, see if/else conditions 
PathPoint PathPlanner::generateCentrePoint(Cone* cone_one, Cone* cone_two, bool& feasible, std::vector<PathPoint>&cenPoints_temp)
{
	PathPoint midpoint(
		(cone_one->position.x + cone_two->position.x) / 2,
		(cone_one->position.y + cone_two->position.y) / 2
	);
	
	// get distance between the 2 cones, if too far or too near, not feasible
	float dist = calcDist(cone_one->position,cone_two->position);
	if ((dist > TRACKWIDTH*1.5)|| (dist < TRACKWIDTH*0.5))
	{
		if (DEBUG) std::cout << "[XX] Rejected point: (" << midpoint.x << ", " << midpoint.y << ")  cones too far or too near!"<<std::endl;
		midpoint.cone1 = cone_one;
		midpoint.cone2 = cone_two;
		rejected_points.push_back(midpoint);
		feasible = false;
		return midpoint;
	}
	

	// calc the distance to the latest path point
	float dist_back1 = calcDist(cenPoints_temp.back(), midpoint);
	float dist_back2 = calcDist(*(cenPoints_temp.end()-2), midpoint);

	//calc the angle difference
	float angle1 = calcRelativeAngle(cenPoints_temp.back(),midpoint); 
	float angle2 = calcRelativeAngle(*(cenPoints_temp.end()-2),cenPoints_temp.back());
	float angle = angle1 - angle2; //same as calcAngle(...)
	
	
	if ((abs(angle)<MAX_PATH_ANGLE1 ||abs(angle)>MAX_PATH_ANGLE2) && (dist_back1>MIN_POINT_DIST) && (dist_back1<MAX_POINT_DIST))
	{
		// if (DEBUG) std::cout << "Accepted path point: (" << midpoint.x << ", " << midpoint.y << ") ";
		// if (DEBUG) std::cout << "dist and angle: " << dist_back << " " << angle << std::endl;
		feasible = true;
		//record the cones
		midpoint.cone1 = cone_one;
		midpoint.cone2 = cone_two;
		midpoint.angle = angle;
	}
	else
	{
		if (DEBUG)
		{
			std::cout << "[XX] Rejected point: (" << midpoint.x << ", " << midpoint.y << ") ";
			std::cout<<"previous points: ("<<cenPoints_temp.back().x<<", "<<cenPoints_temp.back().y<<")";
			std::cout<<" ("<<(*(cenPoints_temp.end()-2)).x<<", "<<(*(cenPoints_temp.end()-2)).y<<")";
			std::cout << " dist and angle: " << dist_back1 << " " << angle1<<" - "<<angle2 <<std::endl;
		}
		feasible = false;

		// record the cones
		midpoint.cone1 = cone_one;
		midpoint.cone2 = cone_two;
		rejected_points.push_back(midpoint);
	}

	return midpoint;
}

// adds new path points to vector centre_points. uses generateCentrePoint()
void PathPlanner::addCentrePoints()
{
	if (left_cones.empty() || right_cones.empty())
		return;

	bool feasible;
	PathPoint cp;
	int indx1,indx2;
	Cone* opp_cone;
	cenPoints_temp1.clear();
	cenPoints_temp2.clear();

	cenPoints_temp1.push_back(*(centre_points.end()-2));
	cenPoints_temp2.push_back(*(centre_points.end()-2));
	cenPoints_temp1.push_back(centre_points.back());
	cenPoints_temp2.push_back(centre_points.back());

	int c = 0;
	for (int i = leftIndx; i < left_cones.size(); i++)
	{
		if (c==2) //so we only generate 2 new path points
			break;
		// only generate points from cones that havent been passed by yet, or if paired less than 3 times
		if((!left_cones[i]->passedBy)||(left_cones[i]->paired<3))
		{
			opp_cone = findOppositeClosest(*left_cones[i], right_cones);
			feasible = false;
			cp = generateCentrePoint(left_cones[i], opp_cone, feasible,cenPoints_temp1);
			if (feasible)
			{
				c++;
				// centre_points.push_back(cp);
				cenPoints_temp1.push_back(cp);
				
			}
		}
	}

	c=0;
	for (int i = rightIndx; i < right_cones.size(); i++)
	{
		if (c==2) //so we only generate 2 new path points
			break;
		// only generate points from cones that havent been passed by yet, or if paired less than 3 times
		if((!right_cones[i]->passedBy)||(right_cones[i]->paired<3))
		{
			opp_cone = findOppositeClosest(*right_cones[i], left_cones);
			feasible = false;
			cp = generateCentrePoint(right_cones[i], opp_cone, feasible,cenPoints_temp2);
			if (feasible)
			{
				c++;
				// centre_points.push_back(cp);
				cenPoints_temp2.push_back(cp);
			}
		}
	}

	std::vector<PathPoint> temp;
	temp = cenPoints_temp2;
	bool dup = false;
	//combine the temp cenpoints
	
	for (auto &p1:cenPoints_temp1)
	{
		for(auto &p2:cenPoints_temp2)
		{
			if (calcDist(p1,p2)<=0.1)
			{
				dup=true;
				break;
			}
		}

		if (!dup)
		{
			temp.push_back(p1);
		}
		dup = false;
	}


	sortPathPoints(temp,temp.front()); 
	for (int i=2;i<temp.size();i++) //first 2 in temp are just copies from centre_points
	{
		temp[i].cone1->paired++;
		temp[i].cone1->mapped++;
		temp[i].cone2->paired++;
		temp[i].cone2->mapped++;
		centre_points.push_back(temp[i]);
		
	}

}
//for adding first Centre points at the beginning of race, 
void PathPlanner::addFirstCentrePoints()
{
	sortConesByDist(init_pos);		// sort first seen cones by distance
	centre_points.emplace_back(
		(left_cones.front()->position.x + right_cones.front()->position.x) / 2,
		(left_cones.front()->position.y + right_cones.front()->position.y) / 2
	);
	centre_points.back().cone1 = left_cones.front();
	centre_points.back().cone2 = right_cones.front();
	left_cones.front()->paired++;
	left_cones.front()->mapped++;
	right_cones.front()->paired++;
	right_cones.front()->mapped++;
}

//add new cones to the local vector of cones and sort by colour
void PathPlanner::addCones(std::vector<Cone> &new_cones)
{
	updateStoredCones(new_cones);
	int temp = left_cones.size() + right_cones.size() + timing_cones.size();
	if (DEBUG)
	{
		std::cout<<"\nSLAM gives  "<<new_cones.size()<<" cones.";
		std::cout<<" left saved cones: "<<left_cones.size();
		std::cout<<". right saved cones: "<<right_cones.size();
		std::cout<<". timing cones: "<<timing_cones.size();
		std::cout<<". future cones: "<<future_cones.size()<<std::endl;
	}
	if (gotNewCones || !passedByAll)
	{

		for (auto &cone: future_cones)
		{
			if (cone->colour == 'b')
			{
				left_unsorted.push_back(cone);
				l_cones_sorted = false;
				newConesToSort = true;
			}

			else if (cone->colour == 'y')
			{
				right_unsorted.push_back(cone);
				r_cones_sorted = false;
				newConesToSort = true;
			}
			else //red cones
			{
				
				timing_cones.push_back(cone);
				if (DEBUG) std::cout<<"Timing cones found: "<< timing_cones.size() <<std::endl;	
			
			}
		}
		if (DEBUG)	std::cout<<"l and r future cones: "<<left_unsorted.size()<<" and "<<right_unsorted.size()<<std::endl;
		
	}

	else
		return;	
}

// update the position of the raw cones using new cone pos
void PathPlanner::updateStoredCones(std::vector<Cone>&new_cones)
{
	if (raw_cones.size()!=new_cones.size())
		gotNewCones = true;
		
	float dist;
	//add new cones to raw cones while updating future cones
	for (int i=0; i<new_cones.size();i++)
	{
		if (i==raw_cones.size()) //add newly seen cones
		{
			raw_cones.push_back(new_cones[i]);
			future_cones.push_back(&raw_cones[i]);
		}

		else //update previously seen cones if not yet passed by
		{			
			if (!raw_cones[i].passedBy)
			{
				dist = calcDist(raw_cones[i].position,car_pos); //check if within range
				if(dist<CERTAIN_RANGE)
				{
					raw_cones[i].passedBy = true;
				}
				else //if not yet within range
				{			
					raw_cones[i].updateConePos(new_cones[i].position);
					if (raw_cones[i].colour == 'r')
						timingCalc = false;
					else
						future_cones.push_back(&raw_cones[i]);
				}
			}
			else //(if passed by but not paired, add to future cones again for sorting)
			{
				if (raw_cones[i].paired == 0)
					future_cones.push_back(&raw_cones[i]);
			}
		}
	}

	 
	//update left and right cones
	if (left_cones.size()>0)
	{
		for(int i = left_cones.size()-1;i>=0; i--)
		{
			if (!left_cones[i]->passedBy || left_cones[i]->paired==0)
				{
					left_cones.pop_back();
				}
			else
				{
					leftIndx = i;
					break;
				}
				
		}
	}

	if (right_cones.size()>0)
	{
		for(int i = right_cones.size()-1; i>=0; i--)
		{
			if (!right_cones[i]->passedBy || right_cones[i]->paired==0)
				{
					right_cones.pop_back();
				}
			else
				{
					rightIndx = i;
					break;
				}
		}
	}

	
}


void PathPlanner::updateCentrePoints()
{
	
	int temp = centre_points.size();
	float dist;
	float min_dist = 9000;
	int nearest_indx=-1;
	std::cout<<"centre points size before update: "<<temp<<std::endl;
	if (temp <= 2)
	{	
		std::cout<<"\n";
		return;
	}
	else
	{
		//searchfor the nearest path point
		for (int i = centre_points.size()-1; i>=0; i--)
		{
			dist = calcDist(car_pos,centre_points[i]);
			if (dist<min_dist)
			{
				min_dist = dist;
				nearest_indx = i;
			}
			else
				break;
		}
		
		// pop path points if their cones havent been passed by yet
		for (int i = centre_points.size()-1;i>1;i--)
		{
			if((centre_points[i].cone1 == NULL)||(centre_points[i].cone2 == NULL)) //for orange cones 
			// NULL is used since there can be 4 orange cones
			{
				centre_points.pop_back();
				timingCalc = false;
				if (DEBUG) std::cout<<"timing cones path point popped!"<<std::endl;
			}
			else if(!centre_points[i].cone1->passedBy || !centre_points[i].cone2->passedBy)
			{
				centre_points.back().cone1->paired --;
				centre_points.back().cone2->paired --;
				centre_points.pop_back();
			}
			else
				break;
		}
		 
		//experimental: i want to only have at least 2 path points ahead, so pop
		if (nearest_indx!=-1 || (nearest_indx+2) < centre_points.size()-1)
		{
			for (int i = centre_points.size()-1;i>nearest_indx+2;i--)
			{
				centre_points.back().cone1->paired --;
				centre_points.back().cone2->paired --;
				centre_points.pop_back();
				std::cout<<"  experimental "<<std::endl;
			}
			
	
		}


		std::cout<<"  centre points size after update: "<<centre_points.size()<<std::endl;
	}
}
     

// get midpoint of orange cones
void PathPlanner::centralizeTimingCones()
{
	// there could be 2 cones on each side (4 total), we dont have a way to determine which is left or right
	// taking the average ditance of the timing cones is same as getting their midpoint
		
	if (timing_cones.empty())
		return;
	
	PathPoint avg_point(0, 0);
	
	for (int i = 0; i < timing_cones.size(); i++)
	{
		avg_point.x += timing_cones[i]->position.x; // summation of x positions
		avg_point.y += timing_cones[i]->position.y; // summation of y positions
	} 
	avg_point.x = avg_point.x / timing_cones.size(); // avg x dist
	avg_point.y = avg_point.y / timing_cones.size(); // avg y dist


	// Calc distance to timing cone
	PathPoint coneTemp(timing_cones.front()->position.x,timing_cones.front()->position.y);
	float dist = calcDist(coneTemp, avg_point);
	float angle = calcRelativeAngle(init_pos, avg_point);
	
	// if (dist > 0.1*TRACKWIDTH && dist < TRACKWIDTH && (abs(angle)<10))
	// angle should be ~0degrees,
	if  (dist < TRACKWIDTH && abs(angle)<20)
	{
		startFinish = avg_point;
		if (DEBUG) std::cout << "Average timing cones position calculated  ";
		if (DEBUG) std::cout << "dist: "<<dist<<" angle: "<<angle<<std::endl;
		timingCalc = true;
		// startFinish.cone1 = timing_cones.front();
		// startFinish.cone2 = timing_cones.back();
		startFinish.angle = angle;
		centre_points.push_back(startFinish);
		for (auto &t:timing_cones)
		{
			t->paired++;
		}

	}
	else
	{
		if (DEBUG)
		{
			std::cout << "[XX] Average timing cones position NOT calculated" <<std::endl;
			std::cout << "dist: "<<dist<<" angle: "<<angle<<std::endl;
		}
		timingCalc = false;
	}

		
	
}

// find the closest cone on the opposite side
//this is mostly used on the last cone in the vector,
Cone* PathPlanner::findOppositeClosest(const Cone &cone, const std::vector<Cone*> &cones)
{
	float min_dist = 9999; //large number
	float dist;
	Cone* closest_cone = cones.back();
	int count = 0;
	for (int i = cones.size()-1; i>=0;i--)
	{
		dist = calcDist(cone.position, cones[i]->position);
		if (dist < min_dist)
		{
			count = 0;
			min_dist = dist;
			closest_cone = cones[i];
		}
		// if after 10 cones, the closest cone has not been replaced, break and return current closest cone
		// this keeps it from going through all the cones
		if (count == 10)
			break; 

		count ++;
	}
	return closest_cone;
}

//sorts the cones by distance to car, then adds the closes cone to left/right
void PathPlanner::sortConesByDist(const PathPoint &pos)
{
	if (left_unsorted.empty() || right_unsorted.empty())
		return;

	// Assign distance Cone objects on left
    for (auto &cone: left_unsorted)
    {cone->dist = calcDist(pos, cone->position);}

	// Assign distance to Cone objects on right
    for (auto &cone: right_unsorted) 
    {cone->dist = calcDist(pos, cone->position);}

	//sort both cones_to_add vectors
	if (left_unsorted.size()>1)
    sort(left_unsorted.begin(), left_unsorted.end(), compareConeDist);
	if (right_unsorted.size()>1)
    sort(right_unsorted.begin(), right_unsorted.end(), compareConeDist);

    l_cones_sorted = true;
    r_cones_sorted = true;

	for (auto &cn:left_unsorted)
	{
		left_cones.push_back(cn);
	}
	for (auto &cn:right_unsorted)
	{
		right_cones.push_back(cn);
	}
}
bool PathPlanner::compareConeDist(Cone* const &cone_one, Cone* const &cone_two)
{
    return cone_one->dist < cone_two->dist;
}
bool PathPlanner::comparePointDist(PathPoint& pt1, PathPoint& pt2)
{
	return pt1.dist < pt2.dist;
}
bool PathPlanner::compareConeCost(Cone* const &cone_one, Cone* const &cone_two)
{
    return cone_one->cost < cone_two->cost;
}

/* Calculate the distance between 2 points */
float PathPlanner::calcDist(const PathPoint &p1, const PathPoint &p2)
{
    float x_dist = pow(p2.x - p1.x, 2);
    float y_dist = pow(p2.y - p1.y, 2);

    return sqrt(x_dist + y_dist);
}

void PathPlanner::resetTempConeVectors()
{
	left_unsorted.clear();
	right_unsorted.clear();
	oppSide_cone.clear();
	thisSide_cone.clear();
	future_cones.clear();
	l_cones_sorted = false;
	r_cones_sorted = false;
	newConesToSort = false;
	newConesSorted = false;
	gotNewCones = false;
	if (rejectCount > 5)
	{
		rejected_points.clear();
		rejectCount = 0;
	}
}

void PathPlanner::removeFirstPtr(std::vector<Cone*>& cone_vec)
{
    if (cone_vec.size() > 0 && cone_vec.front() != NULL)
    {
        cone_vec.erase(cone_vec.begin());
    }
}

// cost 1: distance between cones of same colour
float PathPlanner::computeCost1(Cone* &cn1, Cone* &cn2)
{
	return calcDist(cn1->position,cn2->position);
}

// cost 2a: distance between nearest cone from opposite side, used during initialisation when only few cones are seen
float PathPlanner::computeCost2a(Cone* &cn1, std::vector<Cone*> &oppCone1,std::vector<Cone*> &oppCone2)
{
	Cone* opp_cone = findOppositeClosest(*cn1,oppCone1);
	float dist1 = calcDist(cn1->position,opp_cone->position);
	float dist2 = 99; //random large number
	if (oppCone2.size()>0)
	{
		Cone* opp_cone2 = findOppositeClosest(*cn1,oppCone2);
		dist2 = calcDist(cn1->position,opp_cone2->position);
	}

	return std::min(dist1,dist2);
}

// cost 2b: distance between cone nearest to car  from opposite side 
float PathPlanner::computeCost2b(Cone* &cn1, std::vector<Cone*> &oppCone)
{
	for (int i=oppCone.size()-1;i>=0;i--)
	{
		if (oppCone[i]->passedBy)
		{
			return calcDist(cn1->position,oppCone[i]->position);
		}
	}
}

// cost 3: change in track curvature cn2 is sorted cone
float PathPlanner::computeCost3(Cone* &cn1, std::vector<Cone*> &cn2)
{
	if (cn2.size()<2)
		return 0;
	int i = cn2.size()-1;
	float theta1 =  atan2((cn2[i]->position.y - cn2[i-1]->position.y),(cn2[i]->position.x - cn2[i-1]->position.x));
	float theta2 =  atan2((cn2[i]->position.y - cn1->position.y),(cn2[i]->position.x - cn1->position.x));
	return theta1 - theta2;
}


// this function sorts the cones using the cost function,
// then pushes the cones to the sorted vector left/right
void PathPlanner::sortAndPushCone(std::vector<Cone*> &cn)
{
	
	if (cn.size() == 0)
		return;

	float cost, cost1, cost2, cost3;
	int index = 0;
	char colour;
	thisSide_cone.clear();
	oppSide_cone.clear();
	oppSide_cone2.clear();

	if (cn.front()->colour == 'b') //if cone is blue(left)
	{
		colour = 'b';
		thisSide_cone.assign(left_cones.begin(),left_cones.end());
		oppSide_cone.assign(right_cones.begin(),right_cones.end());
		if (right_unsorted.size()>0)
			oppSide_cone2.assign(right_unsorted.begin(),right_unsorted.end());

	}
	else if (cn.front()->colour == 'y') //if cone is yellow(right)
	{
		colour = 'y';
		thisSide_cone.assign(right_cones.begin(),right_cones.end());
		oppSide_cone.assign(left_cones.begin(),left_cones.end());
		if (left_unsorted.size()>0)
			oppSide_cone2.assign(left_unsorted.begin(),left_unsorted.end());
	}
	
	if (DEBUG) std::cout<<"cone to be sorted size: "<<cn.size()<<std::endl;

	if (cn.size()<2) //if only 1 cone is seen, compute cost 2 (dist to opposite side) and compare to track width
	{
		float cost2 = computeCost2a(cn.back(),oppSide_cone,oppSide_cone2); //consider unsorted cone as well or this case
		if (cost2 < TRACKWIDTH*1.25)
		{
			if (colour == 'b')
			{
				left_cones.push_back(cn.back());
			}
			else if (colour == 'y')
			{
				right_cones.push_back(cn.back());
			}
			newConesSorted = true;
			return;
		}
		else
		{
			return;
		}
			

	}
		

	else if (cn.size() > 1)
	{
		for (int i=0;i<cn.size();i++)
		{		
			cost1 = computeCost1(cn[i],thisSide_cone.back());
			cost2 = computeCost2b(cn[i],oppSide_cone);//only consider sorted cones
			cost3 = computeCost3(cn[i],thisSide_cone);

			// might need to add different weights for each cost later,
			// but it works with equal weights for now
			cn[i]->cost = (cost1*cost1) + (2*cost2*cost2) + (1.5*cost3*cost3);
			
		}

		sort(cn.begin(), cn.end(), compareConeCost);

		if (colour == 'b')
		{
			for (auto &c:cn)
			{
				left_cones.push_back(c);
			}
			
			
		}
		else if (colour == 'y')
		{
			for (auto &c:cn)
			{
				right_cones.push_back(c);
			}
		}
		newConesSorted = true;
	}

	

}


