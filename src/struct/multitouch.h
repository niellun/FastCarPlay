#ifndef SRC_STRUCT_MULTITOUCH
#define SRC_STRUCT_MULTITOUCH

#define MUTLITOUCH_MAX_TOUCH 5

class Multitouch
{
public:
    struct Touch
    {
        float x; 
        float y;  
        int state; 
        int id;    
    };

    Multitouch() : _count(0) {}

    bool add(float x, float y, int state, int id)
    {
        if (_count >= MUTLITOUCH_MAX_TOUCH)
            return false;

        _touches[_count++] = { x, y, state, id };
        return true;
    }

    int size() const {
        return _count;
    }

    const Touch& operator[](int index) const {
        return _touches[index];
    }

private:
    Touch _touches[MUTLITOUCH_MAX_TOUCH];
    int _count;
};

#endif /* SRC_STRUCT_MULTITOUCH */
