/*******************************************
*author:CaseZheng
*mail:764307915@qq.com
*date:2016/12/30
*desc:boost库 timer学习代码
********************************************/

#include "public.h："

int TestTimer()
{
	timer t;
	cout << "max timespac:" << t.elapsed_max()/3600 << "h" << endl;
	cout << "min timespac:" << t.elapsed_min() << "s" << endl;
	cout << "now timespac:" << t.elapsed() << "s" << endl;


	return 0;
}