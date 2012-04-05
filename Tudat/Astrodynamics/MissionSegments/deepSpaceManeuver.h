/*    Copyright (c) 2010-2012 Delft University of Technology.
 *
 *    This software is protected by national and international copyright.
 *    Any unauthorized use, reproduction or modification is unlawful and
 *    will be prosecuted. Commercial and non-private application of the
 *    software in any form is strictly prohibited unless otherwise granted
 *    by the authors.
 *
 *    The code is provided without any warranty; without even the implied
 *    warranty of merchantibility or fitness for a particular purpose.
 *
 *    Changelog
 *      YYMMDD    Author            Comment
 *      110224    E. Iorfida        First creation of code.
 *      110406    K. Kumar          Minor modifications.
 *
 *    References
 *
 */

#ifndef TUDAT_DEEP_SPACE_MANEUVER_H
#define TUDAT_DEEP_SPACE_MANEUVER_H

#include "Tudat/Astrodynamics/States/state.h"

namespace tudat
{

//! Deep space maneuver base class.
/*!
 * Deep space maneuver class.
 */
class DeepSpaceManeuver
{
public:

    //! Default constructor.
    /*!
     * Default constructor.
     */
    DeepSpaceManeuver( ): deltaV_( -1.0 ), timeOfDeepSpaceManeuver_( 0.0 ),
        pointerToState_( NULL ) { }

    //! Set time of deep space maneuver event.
    /*!
     * Sets time of deep space maneuver event.
     * \param timeOfDeepSpaceManeuver Time of deep space maneuver event.
     */
    void setTime( double timeOfDeepSpaceManeuver )
    { timeOfDeepSpaceManeuver_ = timeOfDeepSpaceManeuver; }

    //! Set state at deep space maneuver event.
    /*!
     * Sets pointer to state at deep space maneuver event.
     * \param pointerToState Pointer to state at deep space maneuver event.
     */
    void setState( State* pointerToState ) { pointerToState_ = pointerToState; }

    //! Set delta-V of deep space maneuver event.
    /*!
     * Sets delta-V of deep space maneuver event.
     * \param deltaV Delta-V of deep space maneuver event.
     */
    void setDeltaV( double deltaV ) { deltaV_ = deltaV; }

    //! Get time of deep space maneuver event.
    /*!
     * Returns the the time of deep space maneuver event.
     * \return Time of deep space maneuver event.
     */
    double getTime( ) { return timeOfDeepSpaceManeuver_; }

    //! Get state at deep space maneuver event.
    /*!
     * Returns a pointer to state at deep space maneuver event.
     * \return Pointer to state at deep space maneuver event.
     */
    State* getState( ) { return pointerToState_; }

    //! Get delta-V of deep space maneuver event.
    /*!
     * Returns delta-V of deep space maneuver event.
     * \return Delta-V of deep space maneuver event.
     */
    double getDeltaV( ) { return deltaV_; }

protected:

private:

    //! Delta-V of deep space maneuver event.
    /*!
     * Delta-V of deep space maneuver event.
     */
    double deltaV_;

    //! Time of deep space maneuver event.
    /*!
     * Time of deep space maneuver event.
     */
    double timeOfDeepSpaceManeuver_;

    //! Pointer to state at deep space maneuver event.
    /*!
     * Pointer to state at deep space maneuver event.
     */
    State* pointerToState_;
};

} // namespace tudat

#endif // TUDAT_DEEP_SPACE_MANEUVER_H