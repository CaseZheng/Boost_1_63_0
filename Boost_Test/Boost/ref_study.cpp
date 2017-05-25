#include "public.h"

#if 0
int main(int argc, char *argv[])
{
    int x = 10;
    reference_wrapper<int> rw(x);
    cout<<(x==rw)<<endl;

    (int &)rw = 100;
    cout<<(100==rw)<<endl;

    reference_wrapper<int> rw2(rw);
    cout<<(rw2.get()==100)<<endl;

    string str;
    reference_wrapper<string> rws(str);
    *rws.get_pointer() = "ref";
    cout<<str<<endl;
    cout<<rws.get().size()<<endl;


    return 0;
}
#endif

#if 0
int main(int argc, char *argv[])
{
    double x = 2.12345;
    BOOST_AUTO(rw, cref(x));
    cout<<typeid(rw).name()<<endl;

    string str = "hahahahah";
    BOOST_AUTO(rws, ref(str));
    cout<<typeid(rws).name()<<endl;

    cout<<sqrt(ref(x))<<endl;
    return 0;
}
#endif

#if 1
int main(int argc, char *argv[])
{
    vector<int> v(10, 2);
    BOOST_AUTO(rw, ref(v));
    cout<<is_reference_wrapper<BOOST_TYPEOF(rw)>::value<<endl;
    cout<<is_reference_wrapper<BOOST_TYPEOF(v)>::value<<endl;

    cout<<typeid(unwrap_reference<BOOST_TYPEOF(rw)>::type).name()<<endl;
    cout<<typeid(unwrap_reference<BOOST_TYPEOF(v)>::type).name()<<endl;

    int x = (int)2.12345;
    BOOST_AUTO(rwd, cref(x));
    cout<<is_reference_wrapper<BOOST_TYPEOF(rwd)>::value<<endl;
    cout<<is_reference_wrapper<BOOST_TYPEOF(x)>::value<<endl;

    cout<<typeid(unwrap_reference<BOOST_TYPEOF(rwd)>::type).name()<<endl;
    cout<<typeid(unwrap_reference<BOOST_TYPEOF(x)>::type).name()<<endl;

    unwrap_ref(rw).push_back(4);
    cout<<unwrap_ref(rw)[10]<<endl;
    
    string str("hahaha");
    BOOST_AUTO(rws, cref(str));
    cout<<unwrap_ref(rws)<<endl;

    return 0;
}
#endif

