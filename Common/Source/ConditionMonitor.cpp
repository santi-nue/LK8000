/*
   LK8000 Tactical Flight Computer -  WWW.LK8000.IT
   Released under GNU/GPL License v.2 or later
   See CREDITS.TXT file for authors and copyrights

   $Id: ConditionMonitor.cpp,v 8.2 2010/12/10 23:51:14 root Exp root $
*/

#include "externs.h"
#include "LKProcess.h"
#include "InputEvents.h"
#include "Library/TimeFunctions.h"

class ConditionMonitor {
public:
  ConditionMonitor(double interval_Notification, double interval_Check):
    LastTime_Notification(-1), LastTime_Check(-1), Interval_Notification(interval_Notification), Interval_Check(interval_Check)
  {
  }

  virtual ~ConditionMonitor() {}

  void Update(NMEA_INFO *Basic, DERIVED_INFO *Calculated) {
    if (!Calculated->Flying)
      return;

    bool restart = false;
    if (Ready_Time_Check(Basic->Time, &restart)) {
      LastTime_Check = Basic->Time;
      if (CheckCondition(Basic, Calculated)) {
        if (Ready_Time_Notification(Basic->Time) && !restart) {
          LastTime_Notification = Basic->Time;
          Notify();
          SaveLast();
        }
      }
      if (restart) {
        SaveLast();
      }
    }
  }

protected:
  double LastTime_Notification;
  double LastTime_Check;
  double Interval_Notification;
  double Interval_Check;

private:

  virtual bool CheckCondition(NMEA_INFO *Basic, DERIVED_INFO *Calculated) = 0;
  virtual void Notify(void) = 0;
  virtual void SaveLast(void) = 0;

  bool Ready_Time_Notification(double T) {
    if (T<=0) {
      return false;
    }
    if ((T<LastTime_Notification) || (LastTime_Notification== -1)) {
      return true;
    }
    if (T>= LastTime_Notification + Interval_Notification) {
      return true;
    }
    return false;
  }

  bool Ready_Time_Check(double T, bool *restart) {
    if (T<=0) {
      return false;
    }
    if ((T<LastTime_Check) || (LastTime_Check== -1)) {
      LastTime_Notification = -1;
      *restart = true;
      return true;
    }
    if (T>= LastTime_Check + Interval_Check) {
      return true;
    }
    return false;
  }
};


///////


class ConditionMonitorWind: public ConditionMonitor {
public:
  ConditionMonitorWind():
    ConditionMonitor(60*5, 10), wind_mag(), wind_bearing(), last_wind_mag(), last_wind_bearing()
  {
  }
  virtual ~ConditionMonitorWind() {}
protected:

  bool CheckCondition(NMEA_INFO *Basic, DERIVED_INFO *Calculated) {

    wind_mag = Calculated->WindSpeed;
    wind_bearing = Calculated->WindBearing;

    if (!Calculated->Flying) {
      last_wind_mag = wind_mag;
      last_wind_bearing = wind_bearing;
      return false;
    }

    double mag_change = fabs(wind_mag - last_wind_mag);
    double dir_change = fabs(AngleLimit180(wind_bearing-last_wind_bearing));
    if (mag_change > Units::From(unKnots, 5)) {
      return true;
    }
    if ((wind_mag > Units::From(unKnots, 10)) && (dir_change > 45)) {
      return true;
    }
    return false;
  }

  void Notify(void) {
	// LKTOKEN  _@M616_ = "Significant wind change"
    DoStatusMessage(MsgToken<616>());
  };

  void SaveLast(void) {
    last_wind_mag = wind_mag;
    last_wind_bearing = wind_bearing;
  }

private:
  double wind_mag;
  double wind_bearing;
  double last_wind_mag;
  double last_wind_bearing;

};


class ConditionMonitorFinalGlide: public ConditionMonitor {
public:
  ConditionMonitorFinalGlide(): ConditionMonitor(60*5, 1), tad(), last_tad() {}
  virtual ~ConditionMonitorFinalGlide() {}
protected:

  bool CheckCondition(NMEA_INFO *Basic, DERIVED_INFO *Calculated) {
    if (!Calculated->Flying || !ValidTaskPoint(ActiveTaskPoint)) {
      return false;
    }

    tad = Calculated->TaskAltitudeDifference*0.2+0.8*tad;

    bool BeforeFinalGlide =
      (ValidTaskPoint(ActiveTaskPoint+1) && !Calculated->FinalGlide);

    if (BeforeFinalGlide) {
      Interval_Notification = 60*5;
      if ((tad>50) && (last_tad< -50)) {
        // report above final glide early
        return true;
      } else if (tad< -50) {
        last_tad = tad;
      }
    } else {
      Interval_Notification = 60;
      if (Calculated->FinalGlide) {
        if ((last_tad< -50) && (tad>1)) {
          // just reached final glide, previously well below
          return true;
        }
        if ((last_tad> 1) && (tad< -50)) {
          // dropped well below final glide, previously above
          last_tad = tad;
          return true; // JMW this was true before
        }
      }
    }
    return false;
  }

  void Notify(void) {
    if (tad>1) {
      InputEvents::processGlideComputer(GCE_FLIGHTMODE_FINALGLIDE_ABOVE);
    }
    if (tad<-1) {
      InputEvents::processGlideComputer(GCE_FLIGHTMODE_FINALGLIDE_BELOW);
    }
  }

  void SaveLast(void) {
    last_tad = tad;
  }

private:
  double tad;
  double last_tad;
};



class ConditionMonitorSunset: public ConditionMonitor {
public:
  ConditionMonitorSunset(): ConditionMonitor(60*30, 60) {}
  virtual ~ConditionMonitorSunset() {}
protected:

  bool CheckCondition(NMEA_INFO *Basic, DERIVED_INFO *Calculated) {

    if (!IsValidTaskTimeToGo(*Calculated)) {
      return false;
    }

    if (!Calculated->Flying || !ValidTaskPoint(ActiveTaskPoint)) {
      return false;
    }

    const int wpt_index = Task[ActiveTaskPoint].Index;
    const WAYPOINT& wpt = WayPointList[wpt_index];

    // THIS IS BUGGY IN NORTHERN EMISPHERE, TODO DISCOVER WHY
    const unsigned sunset_time = DoSunEphemeris( wpt.Longitude, wpt.Latitude);
    const unsigned local_time = LocalTime(Basic->Time);
    const unsigned task_eta =  local_time + Calculated->TaskTimeToGo;

    return (task_eta > sunset_time) && (local_time < sunset_time);
  }

  void Notify(void) {
    // Expect arrival past sunset
    DoStatusMessage(MsgToken<1531>());
  };

  void SaveLast(void) {

  }
};


class ConditionMonitorAATTime: public ConditionMonitor {
public:
  ConditionMonitorAATTime(): ConditionMonitor(60*15, 10) {}
  virtual ~ConditionMonitorAATTime() {}
protected:

  bool CheckCondition(NMEA_INFO *Basic, DERIVED_INFO *Calculated) {
    if (DoOptimizeRoute() || (gTaskType != task_type_t::AAT) || !ValidTaskPoint(ActiveTaskPoint)
        || !(Calculated->ValidStart && !Calculated->ValidFinish)
        || !Calculated->Flying) {
      return false;
    }
    bool OnFinalWaypoint = !ValidTaskPoint(ActiveTaskPoint);
    if (OnFinalWaypoint) {
      // can't do much about it now, so don't give a warning
      return false;
    }
    return (Calculated->TaskTimeToGo < Calculated->AATTimeToGo);
  }

  void Notify(void) {
	// LKTOKEN  _@M270_ = "Expect early task arrival"
    DoStatusMessage(MsgToken<270>());
  }

  void SaveLast(void) {

  }
};


class ConditionMonitorStartRules: public ConditionMonitor {
public:
  ConditionMonitorStartRules(): ConditionMonitor(60, 1), withinMargin() {}
  virtual ~ConditionMonitorStartRules() {}
protected:

  bool CheckCondition(NMEA_INFO *Basic, DERIVED_INFO *Calculated) {
    if (!ValidTaskPoint(ActiveTaskPoint) || !Calculated->Flying
        || (ActiveTaskPoint>0) || !ValidTaskPoint(ActiveTaskPoint+1)) {
      return false;
    }
    if (Calculated->LegDistanceToGo>StartRadius) {
      return false;
    }

    withinMargin = ValidStartSpeed(Basic, Calculated, StartMaxSpeedMargin) &&
                   InsideStartHeight(Basic, Calculated, StartMaxHeightMargin);

    return !(ValidStartSpeed(Basic, Calculated)
	     && InsideStartHeight(Basic, Calculated));
  }

  void Notify(void) {
    if (withinMargin)
	// LKTOKEN  _@M652_ = "Start rules violated\r\nbut within margin"
      DoStatusMessage(MsgToken<652>());
    else
	// LKTOKEN  _@M651_ = "Start rules violated"
      DoStatusMessage(MsgToken<651>());
  }

  void SaveLast(void) {

  }

private:
  bool withinMargin;
};


class ConditionMonitorGlideTerrain: public ConditionMonitor {
public:
  ConditionMonitorGlideTerrain(): ConditionMonitor(60*5, 1), fgtt(), fgtt_last() {}
  virtual ~ConditionMonitorGlideTerrain() {}

protected:

  bool CheckCondition(NMEA_INFO *Basic, DERIVED_INFO *Calculated) {
    if (!Calculated->Flying || !ValidTaskPoint(ActiveTaskPoint)) {
      return false;
    }

    fgtt = !((Calculated->TerrainWarningLatitude == 0.0) &&
	     (Calculated->TerrainWarningLongitude == 0.0));

    if (!Calculated->FinalGlide || (Calculated->TaskAltitudeDifference<-50)) {
      fgtt_last = false;
    } else if ((fgtt) && (!fgtt_last)) {
      // just reached final glide, previously well below
      return true;
    }
    return false;
  }

  void Notify(void) {
    InputEvents::processGlideComputer(GCE_FLIGHTMODE_FINALGLIDE_TERRAIN);
  }

  void SaveLast(void) {
    fgtt_last = fgtt;
  }

private:
  bool fgtt;
  bool fgtt_last;
};



namespace {
  ConditionMonitorWind         cm_wind;
  ConditionMonitorFinalGlide   cm_finalglide;
  ConditionMonitorSunset       cm_sunset;
  ConditionMonitorAATTime      cm_aattime;
  ConditionMonitorStartRules   cm_startrules;
  ConditionMonitorGlideTerrain cm_glideterrain;
}

void ConditionMonitorsUpdate(NMEA_INFO *Basic, DERIVED_INFO *Calculated) {
  cm_wind.Update(Basic, Calculated);
  cm_finalglide.Update(Basic, Calculated);
  cm_sunset.Update(Basic, Calculated);
  cm_aattime.Update(Basic, Calculated);
  cm_startrules.Update(Basic, Calculated);
  cm_glideterrain.Update(Basic, Calculated);
}
